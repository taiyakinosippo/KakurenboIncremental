// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class KakurenboIncremental : ModuleRules
{
	public KakurenboIncremental(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

		// サブフォルダ（Tests など）からもモジュール直下のヘッダを #include できるようにする
		PrivateIncludePaths.Add(ModuleDirectory);

		// Slate/SlateCore: HUD の文字描画（FSlateFontInfo）で使う
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
