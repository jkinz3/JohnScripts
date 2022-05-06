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

#define LOCTEXT_NAMESPACE "FJohnScripts"

DEFINE_LOG_CATEGORY(LogJohnScripts);

void FJohnScripts::InstallHooks()
{
	//This is called by the module's startup function

	//In order to add options to the context menu, UE provides some delegates that are fired off when constructing it. This allows us to intercept it and add our own options
	//So the jist is that when a context menu is constructed (usually on right click), the function OnExtendLevelEditorContextMenu will be called
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked <FLevelEditorModule>("LevelEditor");
	auto& MenuExtenders = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
	MenuExtenders.Add(FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic(&OnExtendLevelEditorActorContextMenu));
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
					) //oh lord what have I done
				); //the land grows quiet
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

					//crete an FDynamicMesh3
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

#undef LOCTEXT_NAMESPACE