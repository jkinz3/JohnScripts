#pragma once
#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogJohnScripts, Log, All);

class FJohnScripts
{
public:

	static void InstallHooks();

	static TSharedRef<FExtender> OnExtendLevelEditorActorContextMenu(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors);

	static void CreateJohnScriptsMenu(FMenuBuilder& MenuBuilder);

	static void GenerateLightmapsForSelectedActors();

	static void SnapStaticMeshVerticesToGrid();
};