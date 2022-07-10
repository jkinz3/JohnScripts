#pragma once
#include "CoreMinimal.h"
#include "Editor/ContentBrowser/Public/ContentBrowserDelegates.h"


DECLARE_LOG_CATEGORY_EXTERN(LogJohnScripts, Log, All);

class FJohnScripts
{
public:

	static void InstallHooks();

	static TSharedRef<FExtender> OnExtendLevelEditorActorContextMenu(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors);

	static TSharedRef<FExtender> OnExtendContentBrowserCommands(const TArray<FAssetData>& InSelectedAssets);

	static void CreateMeshActionSubMenu(FMenuBuilder& MenuBuilder, const TArray<FAssetData> InSelectedAssets);


	static void CreateJohnScriptsMenu(FMenuBuilder& MenuBuilder);

	/*This is pretty easy to understand. Generates lightmaps, sets resolution to 64 the current res is 4, and sets lightmap
	coordinate index to 1*/
	static void GenerateLightmapsForSelectedActors();

	/*Snaps the static mesh verts to the grid in world space. Obviously not the best idea for all meshes*/
	static void SnapStaticMeshVerticesToGrid();

	/*Gens poly groups based off of face angle, then simplies the geo by polygroup*/
	static void GenPolyGroupsAndOptimize();

	/*Meant for megascans 3D plants that have crappy LODs. This sets the lod group to Foliage and generates 5 lods*/
	static void SetLODGroupToFoliageAndGenerateLODs(const TArray<FAssetData> InSelectedAssets);

	static const FName LODGroupGenLODsName;
	static const FName LODGroupFoliageName;
};