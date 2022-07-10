#include "JohnScripts.h"
#include "ToolMenus.h"
#include "Selection.h"
#include "LevelEditor.h"
#include "Engine/StaticMeshActor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "MeshConversion/Public/MeshDescriptionToDynamicMesh.h"
#include "MeshConversion/Public/DynamicMeshToMeshDescription.h"
#include "CleaningOps/SimplifyMeshOp.h"
#include "Polygroups/PolygroupsGenerator.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMeshEditor.h"
#include "DynamicMesh/MeshNormals.h"
#include "MeshConstraints.h"
#include "Properties/RemeshProperties.h"
#include "IMeshReductionManagerModule.h"
#include "Modules/ModuleManager.h"
#include "IMeshReductionManagerModule.h"
#include "IMeshReductionInterfaces.h"
#include "ContentBrowserModule.h"
#include "RenderingThread.h"

#define LOCTEXT_NAMESPACE "FJohnScripts"

DEFINE_LOG_CATEGORY(LogJohnScripts);

const FName FJohnScripts::LODGroupGenLODsName = TEXT("LODGroupGenLODs");
const FName FJohnScripts::LODGroupFoliageName = TEXT("Foliage");





void FJohnScripts::InstallHooks()
{
	//This is called by the module's startup function

	//In order to add options to the context menu, UE provides some delegates that are fired off when constructing it. This allows us to intercept it and add our own options
	//So the jist is that when a context menu is constructed (usually on right click), the function OnExtendLevelEditorContextMenu will be called
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked <FLevelEditorModule>("LevelEditor");
	auto& MenuExtenders = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
	MenuExtenders.Add(FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic(&OnExtendLevelEditorActorContextMenu));

	//This is for the content browser extenders
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBListActionDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBListActionDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&FJohnScripts::OnExtendContentBrowserCommands));

}

TSharedRef<FExtender> FJohnScripts::OnExtendLevelEditorActorContextMenu(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors)
{
	//we create a new FExtender and use an FMenuExtensionDelegate to call CreateJohnScriptsMenu. When it does so, it'll pass an FMenubuilder and THATS what we use to inject our own options
	TSharedRef<FExtender> Extender(new FExtender());
	Extender->AddMenuExtension(
		"ActorOptions",
		EExtensionHook::After,
		CommandList,
		FMenuExtensionDelegate::CreateStatic(&FJohnScripts::CreateJohnScriptsMenu)
	);

	return Extender;
}

TSharedRef<FExtender> FJohnScripts::OnExtendContentBrowserCommands(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender(new FExtender());

	//iterate over all selected assets in the content browser and check for static meshes
	bool bAnyMeshes = false;
	for (auto AssetIt = SelectedAssets.CreateConstIterator(); AssetIt; ++AssetIt)
	{
		const FAssetData& Asset = *AssetIt;
		bAnyMeshes = bAnyMeshes || (Asset.AssetClass == UStaticMesh::StaticClass()->GetFName());

	}
	if (bAnyMeshes)
	{
		Extender->AddMenuExtension(
			"GetAssetActions",
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateStatic(&FJohnScripts::CreateMeshActionSubMenu, SelectedAssets)
		);
	}
	return Extender;
}

void FJohnScripts::CreateMeshActionSubMenu(FMenuBuilder& MenuBuilder, const TArray<FAssetData> SelectedAssets)
{

	MenuBuilder.AddSubMenu(
		LOCTEXT("JohnScriptsActionsName", "John Scripts"),
		LOCTEXT("JohnScriptsTooltip", "John Scripts"),
		FNewMenuDelegate::CreateLambda([SelectedAssets](FMenuBuilder& InMenuBuilder)
			{
				InMenuBuilder.AddMenuEntry(
					LOCTEXT("LODGroupGenLabel", "Set LOD Group"),
					LOCTEXT("LODGroupGenToolTip", "Set LOD Group to Foliage and generate LODs"),
					FSlateIcon(),
					FExecuteAction::CreateLambda([SelectedAssets]
						{
							SelectedAssets;
							SetLODGroupToFoliageAndGenerateLODs(SelectedAssets);
						}
					)
				);
			}

	));
}



void FJohnScripts::CreateJohnScriptsMenu(FMenuBuilder& MenuBuilder)
{
	//This adds in our submenu entries. It's lambda hell down here
	MenuBuilder.AddSubMenu(
		LOCTEXT("JohnScriptsActionsName", "John Scripts"),
		LOCTEXT("JohnScriptsTooltip", "John Scripts"),
		FNewMenuDelegate::CreateLambda([&](FMenuBuilder& InMenuBuilder)
			{
				InMenuBuilder.AddMenuEntry(
					LOCTEXT("GenereateLightMapsText", "Generate LightMaps"),
					LOCTEXT("GenerateLightMapsTooltip", "Generate LightMaps for selected static meshes"),
					FSlateIcon(),
					FExecuteAction::CreateLambda([&]
						{
							GenerateLightmapsForSelectedActors();
						}
					)
				);

				InMenuBuilder.AddMenuEntry(
					LOCTEXT("SnapToGridText", "Snap Mesh Vertices To Grid"),
					LOCTEXT("SnapToGridTooltip", "Snap vertices of selected static meshes to the grid"),
					FSlateIcon(),
					FExecuteAction::CreateLambda([&]
						{
							SnapStaticMeshVerticesToGrid();
						}
					) 
				);

				InMenuBuilder.AddMenuEntry(
					LOCTEXT("GenPolyGroupsText", "Gen PolyGroups and Optimize"),
					LOCTEXT("GenPolyGroupsToolTip", "Generates Polygroups, Box Unwraps, then simplifies."),
					FSlateIcon(),
					FExecuteAction::CreateLambda([&]
						{
							GenPolyGroupsAndOptimize();
						}
					)//oh lord what have I done
				);//the land grows quiet
			} //the darkness is suffocating
		) //I can hear the laughter of evil things
	); //all is nothing. nothing is all
}

void FJohnScripts::GenerateLightmapsForSelectedActors()
{
	FNotificationInfo Info(LOCTEXT("GenerateLightmapNotification", "Generating lightmaps for static meshes"));
	Info.ExpireDuration = 5.f;
	FSlateNotificationManager::Get().AddNotification(Info);
	GEditor->BeginTransaction(FText::FromString("GenerateLightmaps"));
	int32 NumModelsModified = 0;
	for (auto It = GEditor->GetSelectedActorIterator(); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			if (AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor))
			{

				if (UStaticMesh* StaticMesh = StaticMeshActor->GetStaticMeshComponent()->GetStaticMesh())
				{
					StaticMesh->Modify();
					int32 ModelCount = StaticMesh->GetNumSourceModels();
					for (int32 LODIndex = 0; LODIndex < ModelCount; ++LODIndex)
					{
						FStaticMeshSourceModel& SrcModel = StaticMesh->GetSourceModel(LODIndex);
						SrcModel.BuildSettings.bGenerateLightmapUVs = true;
					}

					//only designed to be used with meshes that have only 1 UV channel. Potential improvement in the future.
					StaticMesh->SetLightMapCoordinateIndex(1);
					if (StaticMesh->GetLightMapResolution() == 4)
					{
						StaticMesh->SetLightMapResolution(64);

					}
					StaticMesh->PostEditChange();
					NumModelsModified++;
				}
			}
		}
	}
	UE_LOG(LogJohnScripts, Warning, TEXT("Generated lightmaps for %d models"), NumModelsModified);
	GEditor->EndTransaction();
}

void FJohnScripts::SnapStaticMeshVerticesToGrid()
{
	FNotificationInfo Info(LOCTEXT("SnapToGridNotification", "Snapping Selected Mesh's Vertices To Grid"));
	Info.ExpireDuration = 5.f;
	FSlateNotificationManager::Get().AddNotification(Info);
	GEditor->BeginTransaction(FText::FromString("SnapMeshVerticesToGrid"));

	int32 NumModelsModified = 0;
	const float GridSize = GEditor->GetGridSize();

	for (auto It = GEditor->GetSelectedActorIterator(); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			if (AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor))
			{
				//get the actor's transform. this is so we can transform the vertices from local space -> world space and vice versa
				const FTransform ActorTransform = StaticMeshActor->GetTransform();

				if (UStaticMesh* StaticMesh = StaticMeshActor->GetStaticMeshComponent()->GetStaticMesh())
				{
					if (StaticMesh->NaniteSettings.bEnabled == true)
					{
						UE_LOG(LogJohnScripts, Warning, TEXT("Static Mesh %s is a nanite mesh. Nice try but no. Skipping"), *StaticMesh->GetName());
						continue;
					}

					//Tell the editor we're modifying the static mesh. This marks it dirty so it has to be saved and also adds it to the transaction buffer
					StaticMesh->Modify();

					//all the geometry of static meshes are stored in MeshDescriptions (used to be an older class called RawMesh but that changed a few versions ago.

					//create an FDynamicMesh3
					TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> DynamicMesh = MakeShared<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe>();

					/*
						Here's the flow for how the modeling toolset does things
						1. Convert MeshDescription to FDynamicMesh3
						2. Modify it in some way.
						3. Convert it back to MeshDescription and apply it to the STatic Mesh

						Why?
						StaticMeshes are inherently rigid. They're not meant to be modified and that's why they're so performant and efficient.
						In order to change the geometry data, we need to convert it into a form that's meant to be modified, which is FDynamicMesh3

					*/

					FMeshDescriptionToDynamicMesh Converter;
					Converter.Convert(StaticMesh->GetMeshDescription(0), *DynamicMesh); // converts MeshDescription into FDynamicMesh3

					int32 NumVertices = 0;
					//iterate over all the vertices in the mesh
					for (int32 vid : DynamicMesh->VertexIndicesItr())
					{
						FVector3d LocalPos = DynamicMesh->GetVertex(vid);
						
						//transform vertex into world space before snapping
						const FVector3d WorldPos = ActorTransform.TransformPosition(LocalPos);

						//snap it
						const FVector3d SnappedPos(FMath::RoundToDouble(WorldPos.X / GridSize) * GridSize, //maths. This code is taken from the "Snap Brush Vertices to Grid" code
							FMath::RoundToDouble(WorldPos.Y / GridSize) * GridSize,
							FMath::RoundToDouble(WorldPos.Z / GridSize) * GridSize);

						//now transform it back into local space before applying
						const FVector3d SnappedLocalPos = ActorTransform.InverseTransformPosition(SnappedPos);

						DynamicMesh->SetVertex(vid, SnappedLocalPos);
						NumVertices++;
					}

					//check to see if it snapped the model into nothingness
					const float MeshVolume = DynamicMesh->GetBounds().Volume(); //at really large snapping distances, the vertices of the mesh will snap together and make the model lose all volume. 
					if (MeshVolume == 0.f)
					{
						UE_LOG(LogJohnScripts, Warning, TEXT("Static Mesh %s will snap into nothingness. The grid size is probably wayyyy too big compared to the mesh. Skipping"), *StaticMesh->GetName());
						continue;
					}

					//now convert it back to a mesh description and apply it to the static mesh
					FMeshDescription* NewMeshDescription = StaticMesh->GetMeshDescription(0);
					if (NewMeshDescription == nullptr)
					{
						NewMeshDescription = StaticMesh->CreateMeshDescription(0); // if the mesh doesn't have a mesh description, create one (no idea why this would happen)
					}
					StaticMesh->ModifyMeshDescription(0); //mark it as dirty like with the actual actor

					FConversionToMeshDescriptionOptions Options;
					FDynamicMeshToMeshDescription ToConverter(Options);

					ToConverter.Convert(DynamicMesh.Get(), *NewMeshDescription, true); // convert the FDynamicMesh3 to MeshDescription.

					StaticMesh->CommitMeshDescription(0); //this commits the description and builds the static mesh fully and officially. 

					StaticMesh->PostEditChange();
					NumModelsModified++;


				}

			}
		}
	}

	UE_LOG(LogJohnScripts, Warning, TEXT("Snapped vertices to grid for %d models"), NumModelsModified);

	GEditor->EndTransaction();

}

void FJohnScripts::GenPolyGroupsAndOptimize()
{
	FNotificationInfo Info(LOCTEXT("GenPolyGroupsNotification", "Generating PolyGroups and Optimizing"));
	Info.ExpireDuration = 5.f;
	FSlateNotificationManager::Get().AddNotification(Info);
	GEditor->BeginTransaction(FText::FromString("GenPolyGroupsAndOptimize"));


	for (auto It = GEditor->GetSelectedActorIterator(); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			if (AStaticMeshActor* StaticMeshActor = Cast<AStaticMeshActor>(Actor))
			{
				if (UStaticMesh* StaticMesh = StaticMeshActor->GetStaticMeshComponent()->GetStaticMesh())
				{
					//Tell the editor we're modifying the static mesh. This marks it dirty so it has to be saved and also adds it to the transaction buffer
					StaticMesh->Modify();

					TSharedPtr<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe> DynamicMesh = MakeShared<UE::Geometry::FDynamicMesh3, ESPMode::ThreadSafe>();
					TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe> OriginalSpatial = MakeShared<UE::Geometry::FDynamicMeshAABBTree3, ESPMode::ThreadSafe>(DynamicMesh.Get(), true);
					TUniquePtr<FDynamicMesh3> ResultMesh = MakeUnique<FDynamicMesh3>();

					TSharedPtr<FMeshDescription, ESPMode::ThreadSafe> MeshDescription = MakeShared<FMeshDescription, ESPMode::ThreadSafe> (*StaticMesh->GetMeshDescription(0));
					FMeshDescriptionToDynamicMesh Converter;
					Converter.Convert(MeshDescription.Get(), *DynamicMesh); // converts MeshDescription into FDynamicMesh3

					ResultMesh->Copy(*DynamicMesh, true, true, true, true);


					UE::Geometry::FPolygroupsGenerator Generator = UE::Geometry::FPolygroupsGenerator(ResultMesh.Get());
					Generator.MinGroupSize = 2;

					double DotTolerance = 1.0 - FMathd::Cos(.1f * FMathd::DegToRad);
					Generator.FindPolygroupsFromFaceNormals(DotTolerance);

					Generator.FindPolygroupEdges();

					if (ResultMesh->HasAttributes() == false)
					{
						ResultMesh->EnableAttributes();
					}

					UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = ResultMesh->Attributes()->PrimaryNormals();

					NormalOverlay->ClearElements();

					UE::Geometry::FDynamicMeshEditor Editor(ResultMesh.Get());
					for (const TArray<int>& Polygon : Generator.FoundPolygroups)
					{
						FVector3f Normal = (FVector3f)ResultMesh->GetTriNormal(Polygon[0]);
						Editor.SetTriangleNormals(Polygon, Normal);


					}
					UE::Geometry::FMeshNormals Normals(ResultMesh.Get());
					Normals.RecomputeOverlayNormals(ResultMesh->Attributes()->PrimaryNormals());
					Normals.CopyToOverlay(NormalOverlay, false);

					UE::Geometry::FSimplifyMeshOp Op;

					Op.bDiscardAttributes = true;
					Op.bPreventNormalFlips = true;
					Op.bPreserveSharpEdges = true;
					Op.bReproject = false;
					Op.SimplifierType = ESimplifyType::UEStandard;
					Op.TargetMode = ESimplifyTargetType::Percentage;
					Op.TargetPercentage = 50;
					Op.MeshBoundaryConstraint = (UE::Geometry::EEdgeRefineFlags)EMeshBoundaryConstraint::Free;
					Op.GroupBoundaryConstraint = (UE::Geometry::EEdgeRefineFlags)EGroupBoundaryConstraint::Free;
					Op.MaterialBoundaryConstraint = (UE::Geometry::EEdgeRefineFlags)EMaterialBoundaryConstraint::Free;
					Op.OriginalMeshDescription = MeshDescription;
					Op.OriginalMesh = DynamicMesh;
					Op.OriginalMeshSpatial = OriginalSpatial;

					IMeshReductionManagerModule& MeshReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface");
					Op.MeshReduction = MeshReductionModule.GetStaticMeshReductionInterface();
					FProgressCancel Progress;
					Op.CalculateResult(&Progress);

					FDynamicMeshToMeshDescription DynamicMeshToMeshDescription;
					ResultMesh = Op.ExtractResult();
					DynamicMeshToMeshDescription.Convert(ResultMesh.Get(), *MeshDescription);

					UStaticMesh::FCommitMeshDescriptionParams Params;
					Params.bMarkPackageDirty = false;
					Params.bUseHashAsGuid = true;

					StaticMesh->CommitMeshDescription(0, Params);


				}
			}
		}
	}


	GEditor->EndTransaction();
}

void FJohnScripts::SetLODGroupToFoliageAndGenerateLODs(TArray<FAssetData> SelectedAssets)
{
	int LODCount = 5;
	TArray<UObject*> Result;
	for (FAssetData& AssetData : SelectedAssets)
	{
		Result.Add(AssetData.GetAsset());
	}

	TArray<UStaticMesh*> MeshesToModify;

	//iterate and fill array with only static meshes
	if (Result.Num() > 0)
	{
		for (UObject* Object : Result)
		{
			UStaticMesh* NewMesh = Cast<UStaticMesh>(Object);
			if (NewMesh)
			{
				MeshesToModify.Add(NewMesh);
			}
		}

		if (MeshesToModify.Num() > 0)
		{
			FNotificationInfo Info(LOCTEXT("GenLODGroupsNotification", "Setting mesh LOD group to foliage and generating LODs"));
			Info.ExpireDuration = 5.f;
			FSlateNotificationManager::Get().AddNotification(Info);
			GEditor->BeginTransaction(FText::FromString("LODGroupAndGenLODs"));

			for (UStaticMesh* Mesh : MeshesToModify)
			{
				Mesh->SetLODGroup(LODGroupFoliageName);
				Mesh->SetNumSourceModels(LODCount);
				Mesh->PostEditChange();
				
			}


			GEditor->EndTransaction();
		}
	}

}

#undef LOCTEXT_NAMESPACE