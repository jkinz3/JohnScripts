// Copyright Epic Games, Inc. All Rights Reserved.

#include "JohnScripts.h"
#include "ToolMenus.h"
#include "Selection.h"
#include "LevelEditor.h"
#include "Engine/StaticMeshActor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FJohnScriptsModule"

DEFINE_LOG_CATEGORY(LogJohnScripts);


void FJohnScriptsModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FJohnScriptsModule::OnFEngineLoopInitComplete);
}

void FJohnScriptsModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);
}

void FJohnScriptsModule::OnFEngineLoopInitComplete()
{
	ExtendBuildMenu();
}

void FJohnScriptsModule::ExtendBuildMenu()
{
	if(UToolMenu* BuildMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Build"))
	{
		FToolMenuSection& Section = BuildMenu->AddSection("JohnScripts", LOCTEXT("JohnScriptsHeading", "John Scripts"));
		FUIAction GenerateLightmapsAction(
			FExecuteAction::CreateLambda([&]()
				{
					GenerateLightMaps();
				}	
			),
			FCanExecuteAction::CreateLambda([]()
				{
					return true;
				}
			),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateLambda([]()
			{
				return true;
			}
			)

		);

		FToolMenuEntry& Entry = Section.AddMenuEntry(NAME_None, LOCTEXT("GenerateLightmapsTitle", "Generate Lightmaps"),
			LOCTEXT("GenerateLightmapsTooltip", "Generate Lightmaps for selected static mesh actors"), FSlateIcon(), GenerateLightmapsAction, EUserInterfaceActionType::Button);
	}
}

void FJohnScriptsModule::GenerateLightMaps()
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

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FJohnScriptsModule, JohnScripts)