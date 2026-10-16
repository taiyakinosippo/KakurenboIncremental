// テキストの窓口（M16）：パッケージ化したゲームを、外の AI（Tools/AgentPlaytest の Python）が文字だけで遊ぶための入口。
//
// 起動オプション -KakuAgentPort=<番号> で 127.0.0.1:<番号> の TCP で AI を待つ（AI は 1 つ。切れたら次の AI を待つ）。
//   -KakuAgentSpeed=<倍率>  ゲームの時間を速める（学習のため。既定 1）
//   -KakuAgentShots=<フォルダ> スクリーンショットの置き場（shot の操作・見つけた不具合）
//   -KakuAgentLight          軽くする（CPU・GPU を使いすぎないように：コマ数の上限・画質を一番低く・描く解像度を半分。見た目以外は同じ）
//   -KakuAgentMaxFPS=<数>    軽くするときのコマ数の上限（既定：ゲームの速さ × 20。24〜60）
//
// ゲーム → AI（0.1 秒ごと。#OBS から #END まで）
//   PHASE / MENU   今のパートと開いているメニュー
//   SCREEN         画面に描いた文（上から順。s・m の後ろは文字の大きさ。m はメニュー（暗くした画面の上に重ねた画面）の文）
//   BUTTONS        押せるボタン（「種類.番号 | ボタンの文字」。click で押す）
//   LABELS         画面に出ているワールドの名前（「鬼の門」「スタート」「！」）と場所（マス）
//   SELF           自分の体（場所はマス。h は床からの高さ（段）・しゃがんでいるか・よじ登っているか）
//   SEE / HEAR     見えている鬼・お宝（壁や家具にさえぎられていないもの）／聞こえた音（方角 dir＝8 方位・deg＝15 度刻みと、大きさ）
//                  SEE ring は前の観測から後に床に広がった輪（場所・広がりきった半径（マス）・色の名前・線の太さ）。意味は書かない（AI が学ぶ）
//   MAP            上から見た館の形（. 床 / 1 低い家具 / 2 背の高さくらい / 3 2 段ぶん / # 部屋の壁 / g 赤い床）
//   PLACED         置いてある壁と道具（自分が置いたもの）
//   WALK / ACK     歩いている様子・操作の結果
//   GAUGE          文字ではない画面の目盛り（煙幕の残りの四角）を文字にしたもの
//   TEL            記録用の本当の値（ステージ・コイン・鬼の場所など）。AI の判断には使わない約束（報告書と不具合の検出に使う）
//   BUG            ゲームの中で見つけたおかしなこと（はみ出した文・動けない・止まった鬼…）
// AI → ゲーム（1 行 1 つ。「番号 操作 …」）
//   click <種類.番号> / key <操作> tap|down|up / place x y / rclick x y / setstart x y
//   goto x y [sneak] [climb] / stop / face x y / look <角度> / mash <回数/秒> / crouch 0|1 / map / shot <名前> / quit
// 入力は本物と同じ経路（疑似キー・疑似クリック。カーソルはテスト用の位置で、本物のマウスは動かさない）

#include "KakurenboPlayerController.h"

#include "Common/TcpSocketBuilder.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/WorldSettings.h"
#include "GridPathfinder.h"
#include "HAL/FileManager.h"
#include "HiderCharacter.h"
#include "InputKeyEventArgs.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Kismet/GameplayStatics.h"
#include "KakurenboGameMode.h"
#include "KakurenboGameState.h"
#include "KakurenboFx.h"
#include "KakurenboGridSubsystem.h"
#include "KakurenboHUD.h"
#include "KakurenboSettings.h"
#include "KakurenboSoundSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Scalability.h"
#include "OniCharacter.h"
#include "PlaceableBlock.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "TrapActor.h"
#include "TreasureActor.h"
#include "UnrealClient.h"

/** 窓口の状態（PlayerController が TSharedPtr で持つ） */
struct FKakurenboAgentBridge
{
	int32 Port = 0;
	FSocket* Listener = nullptr;
	FSocket* Client = nullptr;
	/** 受け取ったがまだ改行が来ていない分・送りきれていない分 */
	TArray<uint8> RecvBuffer;
	TArray<uint8> SendBuffer;
	FTSTicker::FDelegateHandle Ticker;

	double Clock = 0.0;
	double NextObs = 0.0;
	float ObsInterval = 0.1f;
	float Speed = 1.f;
	double NextSpeedApply = 0.0;
	int32 Seq = 0;
	TArray<FString> Acks;
	TArray<FString> PendingBugs;
	TSet<FString> BugKeys;
	FString ShotDir;
	int32 ShotBudget = 80;
	int32 ShotsTaken = 0;

	// 見取り図（家具の高さは館が変わったときだけ測る）
	bool bSendMap = true;
	FName HeightMap;
	FIntPoint HeightSize = FIntPoint::ZeroValue;
	TArray<float> ObstacleHeight;
	FString LastMapText;
	FString LastPlacedText;
	double NextPlacedResend = 0.0;
	/** 不具合の検出・観測がそれぞれ最後に見たパート */
	EKakurenboPhase LastPhase = EKakurenboPhase::Title;
	bool bPhaseKnown = false;
	EKakurenboPhase ObsPhase = EKakurenboPhase::Title;
	bool bObsPhaseKnown = false;

	// UI・マスの操作は 1 つずつ順に（カーソルを動かす操作がぶつからないように）
	struct FUIAction
	{
		int32 Id = 0;
		FString Verb;
		EKakurenboUIAction Button = EKakurenboUIAction::None;
		int32 ButtonIndex = 0;
		FIntPoint Cell = FIntPoint::ZeroValue;
		int32 Step = 0;
		int32 Frames = 0;
		int32 Tries = 0;
		bool bFallback = false;
		int32 BeforeHeight = 0;
		bool bBeforeTrap = false;
		int32 BeforeDir = -1;
		FIntPoint BeforeStart = FIntPoint::ZeroValue;
		FString Reason;
	};
	TArray<FUIAction> UIQueue;

	// かくれんぼの体
	bool bWalking = false;
	FIntPoint WalkGoal = FIntPoint::ZeroValue;
	TArray<FIntPoint> WalkPath;
	int32 WalkIndex = 0;
	bool bWalkClimb = false;
	double NextClimbTap = 0.0;
	bool bForwardDown = false;
	FVector StuckPos = FVector::ZeroVector;
	double StuckTime = 0.0;
	int32 StuckTries = 0;
	FString WalkState = TEXT("idle");
	float MashRate = 0.f;
	double NextMash = 0.0;
	int32 WantCrouch = -1;
	double NextCrouchTap = 0.0;

	// 不具合の検出
	double NextBugCheck = 0.0;
	struct FOniWatch { FVector Pos = FVector::ZeroVector; double Since = 0.0; };
	TMap<TWeakObjectPtr<AOniCharacter>, FOniWatch> OniWatch;
	double OverrunSince = -1.0;
	FString LastNearestType;
	FString LastNearestState;
	float LastNearestDist = -1.f;
	FString RoundEndEvent;
};

namespace
{
	const TCHAR* AgentPhaseText(EKakurenboPhase Phase)
	{
		switch (Phase)
		{
		case EKakurenboPhase::Shop:   return TEXT("Shop");
		case EKakurenboPhase::Build:  return TEXT("Build");
		case EKakurenboPhase::Hide:   return TEXT("Hide");
		case EKakurenboPhase::Result: return TEXT("Result");
		case EKakurenboPhase::Title:  return TEXT("Title");
		default:                      return TEXT("?");
		}
	}

	const TCHAR* AgentMenuText(EKakurenboMenu Menu)
	{
		switch (Menu)
		{
		case EKakurenboMenu::None:           return TEXT("None");
		case EKakurenboMenu::Pause:          return TEXT("Pause");
		case EKakurenboMenu::Settings:       return TEXT("Settings");
		case EKakurenboMenu::Keys:           return TEXT("Keys");
		case EKakurenboMenu::Help:           return TEXT("Help");
		case EKakurenboMenu::ConfirmNewGame: return TEXT("ConfirmNewGame");
		case EKakurenboMenu::ConfirmTitle:   return TEXT("ConfirmTitle");
		case EKakurenboMenu::OniIntro:       return TEXT("OniIntro");
		default:                             return TEXT("?");
		}
	}

	/** ボタンの種類の名前（click の引数と同じ） */
	const TCHAR* const AgentUIActionNames[] = {
		TEXT("None"), TEXT("ShopTab"), TEXT("BuyAmount"), TEXT("ShopItem"), TEXT("Prestige"), TEXT("NextPhase"), TEXT("BuildSlot"), TEXT("RefillAll"),
		TEXT("BackToShop"), TEXT("ClearAll"), TEXT("TitleItem"), TEXT("OpenMenu"), TEXT("OpenHelp"), TEXT("MenuItem"), TEXT("Slider"), TEXT("Toggle"),
		TEXT("OpenKeys"), TEXT("KeySlot"), TEXT("ResetKeys"), TEXT("ResetTips"), TEXT("Back"), TEXT("ConfirmYes"), TEXT("ConfirmNo"), TEXT("OniPage"),
	};

	const TCHAR* AgentUIActionText(EKakurenboUIAction Action)
	{
		const int32 I = static_cast<int32>(Action);
		return I >= 0 && I < UE_ARRAY_COUNT(AgentUIActionNames) ? AgentUIActionNames[I] : TEXT("?");
	}

	bool AgentParseUIAction(const FString& Name, EKakurenboUIAction& Out)
	{
		for (int32 I = 0; I < UE_ARRAY_COUNT(AgentUIActionNames); ++I)
		{
			if (Name.Equals(AgentUIActionNames[I], ESearchCase::IgnoreCase))
			{
				Out = static_cast<EKakurenboUIAction>(I);
				return true;
			}
		}
		return false;
	}

	/** 1 行に収める（改行・タブを消す） */
	FString AgentClean(const FString& In)
	{
		FString S = In;
		S.ReplaceInline(TEXT("\r"), TEXT(""));
		S.ReplaceInline(TEXT("\n"), TEXT(" / "));
		S.ReplaceInline(TEXT("\t"), TEXT(" "));
		return S.TrimStartAndEnd();
	}

	/** マスの差の向き（E = +X、S = +Y） */
	const TCHAR* AgentDirText(float DX, float DY)
	{
		static const TCHAR* const Names[8] = { TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW"), TEXT("N"), TEXT("NE") };
		const float Deg = FMath::RadiansToDegrees(FMath::Atan2(DY, DX));
		const int32 Sector = (FMath::RoundToInt(Deg / 45.f) % 8 + 8) % 8;
		return Names[Sector];
	}

	/** 色のおおまかな名前（人が見て言う言葉くらいの細かさ。輪の色を AI に伝える） */
	const TCHAR* AgentColorName(const FLinearColor& Color)
	{
		const FLinearColor Hsv = Color.LinearRGBToHSV(); // R = 色相（0〜360）・G = 鮮やかさ・B = 明るさ
		if (Hsv.B < 0.15f)
		{
			return TEXT("black");
		}
		if (Hsv.G < 0.25f)
		{
			return Hsv.B > 0.7f ? TEXT("white") : TEXT("gray");
		}
		const float H = Hsv.R;
		return H < 12.f ? TEXT("red") : H < 38.f ? TEXT("orange") : H < 70.f ? TEXT("yellow") : H < 165.f ? TEXT("green")
			: H < 195.f ? TEXT("cyan") : H < 255.f ? TEXT("blue") : H < 290.f ? TEXT("purple") : H < 340.f ? TEXT("pink") : TEXT("red");
	}

	FString AgentEnumShort(const FString& Full)
	{
		int32 Colon = INDEX_NONE;
		return Full.FindLastChar(TEXT(':'), Colon) ? Full.RightChop(Colon + 1) : Full;
	}
}

// ---------------------------------------------------------------- 開始・通信

void AKakurenboPlayerController::KakuAgent(const FString& Options)
{
	if (Agent)
	{
		return; // もう動いている
	}
	Agent = MakeShared<FKakurenboAgentBridge>();
	FKakurenboAgentBridge& A = *Agent;
	FParse::Value(*Options, TEXT("Port="), A.Port);
	FParse::Value(FCommandLine::Get(), TEXT("KakuAgentSpeed="), A.Speed);
	FParse::Value(*Options, TEXT("Speed="), A.Speed);
	A.Speed = FMath::Clamp(A.Speed, 0.25f, 4.f);
	FParse::Value(FCommandLine::Get(), TEXT("KakuAgentShots="), A.ShotDir);
	FParse::Value(FCommandLine::Get(), TEXT("KakuAgentShotBudget="), A.ShotBudget);
	if (A.ShotDir.IsEmpty())
	{
		A.ShotDir = FPaths::ProjectSavedDir() / TEXT("AgentShots");
	}
	A.ShotDir.ReplaceInline(TEXT("\""), TEXT(""));
	IFileManager::Get().MakeDirectory(*A.ShotDir, true);

	// 127.0.0.1 だけで待つ（同じパソコンの AI だけがつなげる）
	const FIPv4Endpoint Endpoint(FIPv4Address(127, 0, 0, 1), static_cast<uint16>(A.Port));
	A.Listener = FTcpSocketBuilder(TEXT("KakuAgentListener")).AsReusable().AsNonBlocking().BoundToEndpoint(Endpoint).Listening(4).Build();
	if (!A.Listener)
	{
		UE_LOG(LogTemp, Error, TEXT("[Agent] could not listen on 127.0.0.1:%d"), A.Port);
		Agent.Reset();
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[Agent] listening on 127.0.0.1:%d (speed %.2f, shots %s)"), A.Port, A.Speed, *A.ShotDir);

	// 軽くする：何体も同時に動かすので、コマ数に上限を付け、画質を一番低くする（AI は画面の絵を見ないので、判断は変わらない）
	if (FParse::Param(FCommandLine::Get(), TEXT("KakuAgentLight")))
	{
		float MaxFps = FMath::Clamp(20.f * A.Speed, 24.f, 60.f);
		FParse::Value(FCommandLine::Get(), TEXT("KakuAgentMaxFPS="), MaxFps);
		Scalability::FQualityLevels Low;
		Low.SetFromSingleQualityLevel(0);
		Scalability::SetQualityLevels(Low);
		auto SetCVar = [](const TCHAR* Name, float Value)
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Var->Set(Value, ECVF_SetByCode);
			}
		};
		SetCVar(TEXT("t.MaxFPS"), MaxFps);
		SetCVar(TEXT("r.ScreenPercentage"), 50.f);
		UE_LOG(LogTemp, Display, TEXT("[Agent] light mode (max %.0f fps, lowest quality)"), MaxFps);
	}

	// 本物のキーボード・マウスは使わない（AI だけが操作する）。ヒントと鬼のチュートリアルは初めての人と同じく出す
	bIgnoreRealInputForAutoTest = true;
	bShowTipsInAutoTest = true;
	bShowOniIntroInAutoTest = true;
	ApplyAutoTestInputIsolation();
	AKakurenboHUD::bRecordTextLayout = true;

	TWeakObjectPtr<AKakurenboPlayerController> WeakThis(this);
	A.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float Dt) -> bool
	{
		AKakurenboPlayerController* PC = WeakThis.Get();
		if (!PC || !PC->Agent)
		{
			return false;
		}
		PC->TickAgentBridge(FMath::Min(Dt, 0.25f));
		return true;
	}));
}

void AKakurenboPlayerController::TickAgentBridge(float RealDeltaSeconds)
{
	FKakurenboAgentBridge& A = *Agent;
	A.Clock += RealDeltaSeconds;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (UKakurenboSoundSubsystem* Sound = World->GetSubsystem<UKakurenboSoundSubsystem>())
	{
		Sound->bRecordHeard = true;
	}
	if (UKakurenboFxSubsystem* Fx = World->GetSubsystem<UKakurenboFxSubsystem>())
	{
		Fx->bRecordRings = true;
	}
	// ゲームの時間の速さ（何かで戻っても 1 秒ごとにかけ直す）
	if (A.Clock >= A.NextSpeedApply)
	{
		A.NextSpeedApply = A.Clock + 1.0;
		if (World->GetWorldSettings() && !FMath::IsNearlyEqual(World->GetWorldSettings()->TimeDilation, A.Speed, 0.01f))
		{
			UGameplayStatics::SetGlobalTimeDilation(this, A.Speed);
		}
	}

	ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	// AI がつないできたら受け付ける（前の AI が切れていれば入れ替える）
	bool bPending = false;
	if (A.Listener && A.Listener->HasPendingConnection(bPending) && bPending)
	{
		if (FSocket* NewClient = A.Listener->Accept(TEXT("KakuAgentClient")))
		{
			if (A.Client)
			{
				A.Client->Close();
				Sockets->DestroySocket(A.Client);
			}
			A.Client = NewClient;
			A.Client->SetNonBlocking(true);
			A.Client->SetNoDelay(true);
			A.RecvBuffer.Reset();
			A.SendBuffer.Reset();
			A.bSendMap = true;
			A.LastPlacedText.Reset();
			A.UIQueue.Reset();
			UE_LOG(LogTemp, Display, TEXT("[Agent] AI connected"));
		}
	}
	if (!A.Client)
	{
		return;
	}
	if (A.Client->GetConnectionState() == SCS_ConnectionError)
	{
		UE_LOG(LogTemp, Display, TEXT("[Agent] AI disconnected"));
		A.Client->Close();
		Sockets->DestroySocket(A.Client);
		A.Client = nullptr;
		A.MashRate = 0.f;
		A.bWalking = false;
		return;
	}

	// 届いた操作を 1 行ずつ
	uint32 PendingSize = 0;
	while (A.Client->HasPendingData(PendingSize) && PendingSize > 0)
	{
		TArray<uint8> Chunk;
		Chunk.SetNumUninitialized(FMath::Min<uint32>(PendingSize, 65536u));
		int32 Read = 0;
		if (!A.Client->Recv(Chunk.GetData(), Chunk.Num(), Read) || Read <= 0)
		{
			break;
		}
		A.RecvBuffer.Append(Chunk.GetData(), Read);
	}
	int32 LineEnd = INDEX_NONE;
	while (A.RecvBuffer.Find(static_cast<uint8>('\n'), LineEnd))
	{
		TArray<uint8> LineBytes(A.RecvBuffer.GetData(), LineEnd);
		A.RecvBuffer.RemoveAt(0, LineEnd + 1);
		LineBytes.Add(0);
		const FString Line = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(LineBytes.GetData()))).TrimStartAndEnd();
		if (!Line.IsEmpty())
		{
			AgentHandleCommand(Line);
			if (!Agent)
			{
				return; // quit
			}
		}
	}

	AgentTickBuildAction();
	AgentTickBody(RealDeltaSeconds);
	AgentCheckBugs(RealDeltaSeconds);

	// 決まった間隔で、見えているものを送る
	if (A.Clock >= A.NextObs)
	{
		A.NextObs = A.Clock + A.ObsInterval;
		const FString Obs = AgentBuildObservation();
		const FTCHARToUTF8 Utf8(*Obs);
		// 読まれていない分がたまりすぎたら（AI が止まっている）古い分は捨てる
		if (A.SendBuffer.Num() > 4 * 1024 * 1024)
		{
			A.SendBuffer.Reset();
		}
		A.SendBuffer.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
	if (A.SendBuffer.Num() > 0)
	{
		int32 Sent = 0;
		if (A.Client->Send(A.SendBuffer.GetData(), A.SendBuffer.Num(), Sent) && Sent > 0)
		{
			A.SendBuffer.RemoveAt(0, Sent);
		}
	}
}

// ---------------------------------------------------------------- 操作

void AKakurenboPlayerController::AgentHandleCommand(const FString& Line)
{
	FKakurenboAgentBridge& A = *Agent;
	TArray<FString> Parts;
	Line.ParseIntoArrayWS(Parts);
	if (Parts.Num() < 2)
	{
		return;
	}
	const int32 Id = FCString::Atoi(*Parts[0]);
	const FString Verb = Parts[1].ToLower();
	auto Ack = [&A, Id](const FString& Result) { A.Acks.Add(FString::Printf(TEXT("%d %s"), Id, *Result)); };
	auto ArgInt = [&Parts](int32 I, int32 Default = 0) { return Parts.IsValidIndex(I) ? FCString::Atoi(*Parts[I]) : Default; };
	auto HasFlag = [&Parts](const TCHAR* Flag) { for (int32 I = 2; I < Parts.Num(); ++I) { if (Parts[I].Equals(Flag, ESearchCase::IgnoreCase)) { return true; } } return false; };
	AHiderCharacter* Hider = GetHider();
	UKakurenboGridSubsystem* Grid = GetWorld() ? GetWorld()->GetSubsystem<UKakurenboGridSubsystem>() : nullptr;

	if (Verb == TEXT("click") && Parts.Num() >= 3)
	{
		FString ActionName, IndexText;
		FKakurenboAgentBridge::FUIAction Action;
		if (!Parts[2].Split(TEXT("."), &ActionName, &IndexText) || !AgentParseUIAction(ActionName, Action.Button))
		{
			Ack(TEXT("fail unknown-button"));
			return;
		}
		Action.Id = Id;
		Action.Verb = Verb;
		Action.ButtonIndex = FCString::Atoi(*IndexText);
		A.UIQueue.Add(Action);
	}
	else if ((Verb == TEXT("place") || Verb == TEXT("rclick") || Verb == TEXT("setstart")) && Parts.Num() >= 4)
	{
		FKakurenboAgentBridge::FUIAction Action;
		Action.Id = Id;
		Action.Verb = Verb;
		Action.Cell = FIntPoint(ArgInt(2), ArgInt(3));
		A.UIQueue.Add(Action);
	}
	else if (Verb == TEXT("key") && Parts.Num() >= 3)
	{
		const UEnum* Enum = StaticEnum<EKakurenboAction>();
		const int64 Value = Enum ? Enum->GetValueByNameString(FString(TEXT("EKakurenboAction::")) + Parts[2]) : INDEX_NONE;
		if (Value == INDEX_NONE || Value >= static_cast<int64>(EKakurenboAction::Count))
		{
			Ack(TEXT("fail unknown-key"));
			return;
		}
		const EKakurenboAction Key = static_cast<EKakurenboAction>(Value);
		const FString Mode = Parts.IsValidIndex(3) ? Parts[3].ToLower() : TEXT("tap");
		if (Mode == TEXT("down")) { PlaytestKey(Key, true); }
		else if (Mode == TEXT("up")) { PlaytestKey(Key, false); }
		else { PlaytestTap(Key); }
		Ack(TEXT("ok"));
	}
	else if (Verb == TEXT("goto") && Parts.Num() >= 4)
	{
		A.WalkGoal = FIntPoint(ArgInt(2), ArgInt(3));
		const AKakurenboGameMode* Mode = GetKakurenboGameMode();
		A.bWalkClimb = HasFlag(TEXT("climb")) && Mode && Mode->IsClimbUnlocked(); // まだ登れないなら、登る前提の道は選ばない
		A.WantCrouch = HasFlag(TEXT("sneak")) ? 1 : 0;
		A.WalkPath.Reset();
		A.WalkIndex = 0;
		A.StuckTries = 0;
		A.bWalking = false;
		A.WalkState = TEXT("nopath");
		if (Grid && Hider && Grid->IsInside(A.WalkGoal))
		{
			const FIntPoint MyCell = Grid->WorldToCell(Hider->GetActorLocation());
			FKakurenboPathGrid Walk = Grid->BuildWalkGrid();
			if (A.bWalkClimb)
			{
				// よじ登れるなら、登れる高さの壁は少し遠い道として通る（越えて逃げる・囲いの外へ出て戻る。回り道が長いときだけ越える）。
				// 転生のお店の「よじ登る」が ClimbTwoTierLevel からは 2 段にも登れる
				const int32 MaxTier = Mode->CanClimbTwoTiers(Mode->GetPrestigeLevel(EPrestigeUpgrade::Climb)) ? 2 : 1;
				for (int32 Y = 0; Y < Walk.SizeY; ++Y)
				{
					for (int32 X = 0; X < Walk.SizeX; ++X)
					{
						const FIntPoint C(X, Y);
						const int32 Tier = Grid->GetColumnHeight(C);
						if (!Grid->IsObstacle(C) && Tier >= 1 && Tier <= MaxTier)
						{
							Walk.SetExtra(C, 3.f);
						}
					}
				}
			}
			const bool bFound = MyCell == A.WalkGoal || KakurenboPathfinding::FindPath(Walk, MyCell, A.WalkGoal, A.WalkPath);
			if (bFound)
			{
				A.bWalking = A.WalkPath.Num() > 0;
				A.WalkState = A.bWalking ? TEXT("moving") : TEXT("arrived");
				A.StuckPos = Hider->GetActorLocation();
				A.StuckTime = A.Clock;
			}
		}
		Ack(A.WalkState == TEXT("nopath") ? TEXT("fail nopath") : TEXT("ok"));
	}
	else if (Verb == TEXT("stop"))
	{
		A.bWalking = false;
		A.WalkPath.Reset();
		A.WalkState = TEXT("idle");
		Ack(TEXT("ok"));
	}
	else if (Verb == TEXT("face") && Parts.Num() >= 4 && Grid && Hider)
	{
		const FVector To = Grid->CellFloorCenter(FIntPoint(ArgInt(2), ArgInt(3))) - Hider->GetActorLocation();
		const float Yaw = To.Rotation().Yaw;
		SetControlRotation(FRotator(-20.f, Yaw, 0.f));
		Hider->SetActorRotation(FRotator(0.f, Yaw, 0.f));
		Ack(TEXT("ok"));
	}
	else if (Verb == TEXT("look") && Parts.Num() >= 3)
	{
		SetControlRotation(FRotator(-20.f, FCString::Atof(*Parts[2]), 0.f));
		Ack(TEXT("ok"));
	}
	else if (Verb == TEXT("mash") && Parts.Num() >= 3)
	{
		A.MashRate = FMath::Clamp(FCString::Atof(*Parts[2]), 0.f, 12.f);
		Ack(TEXT("ok"));
	}
	else if (Verb == TEXT("crouch") && Parts.Num() >= 3)
	{
		A.WantCrouch = ArgInt(2) != 0 ? 1 : 0;
		Ack(TEXT("ok"));
	}
	else if (Verb == TEXT("map"))
	{
		A.bSendMap = true;
		A.LastPlacedText.Reset();
		Ack(TEXT("ok"));
	}
	else if (Verb == TEXT("shot") && Parts.Num() >= 3)
	{
		if (A.ShotsTaken < A.ShotBudget)
		{
			++A.ShotsTaken;
			const FString Name = FPaths::MakeValidFileName(Parts[2]);
			const FString Path = A.ShotDir / FString::Printf(TEXT("%s_%s.png"), *FDateTime::Now().ToString(TEXT("%H%M%S")), *Name);
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			Ack(FString::Printf(TEXT("ok %s"), *FPaths::GetCleanFilename(Path)));
		}
		else
		{
			Ack(TEXT("fail budget"));
		}
	}
	else if (Verb == TEXT("speed") && Parts.Num() >= 3)
	{
		A.Speed = FMath::Clamp(FCString::Atof(*Parts[2]), 0.25f, 4.f);
		A.NextSpeedApply = 0.0;
		Ack(TEXT("ok"));
	}
	else if (Verb == TEXT("ping"))
	{
		Ack(TEXT("ok"));
	}
	else if (Verb == TEXT("quit"))
	{
		UE_LOG(LogTemp, Display, TEXT("[Agent] quit requested"));
		FTSTicker::GetCoreTicker().RemoveTicker(A.Ticker);
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (A.Client) { A.Client->Close(); Sockets->DestroySocket(A.Client); }
		if (A.Listener) { A.Listener->Close(); Sockets->DestroySocket(A.Listener); }
		Agent.Reset();
		ConsoleCommand(TEXT("quit"));
	}
	else
	{
		Ack(TEXT("fail unknown-command"));
	}
}

void AKakurenboPlayerController::AgentTickBuildAction()
{
	FKakurenboAgentBridge& A = *Agent;
	if (A.UIQueue.Num() == 0)
	{
		return;
	}
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	AKakurenboHUD* KHUD = GetHUD<AKakurenboHUD>();
	AHiderCharacter* Hider = GetHider();
	FKakurenboAgentBridge::FUIAction& Act = A.UIQueue[0];
	auto Finish = [&A](const FString& Result)
	{
		A.Acks.Add(FString::Printf(TEXT("%d %s"), A.UIQueue[0].Id, *Result));
		A.UIQueue.RemoveAt(0);
	};
	if (!GM || !Grid || !KHUD || !Hider || !PlayerInput)
	{
		Finish(TEXT("fail not-ready"));
		return;
	}

	// ---- ボタンを押す：カーソルをボタンの真ん中へ置いてクリック → 次のフレームで終わり
	if (Act.Verb == TEXT("click"))
	{
		if (Act.Step == 0)
		{
			FVector2D Center;
			if (!KHUD->GetButtonCenter(Act.Button, Act.ButtonIndex, Center))
			{
				Finish(TEXT("fail no-such-button"));
				return;
			}
			bUseTestCursor = true;
			TestCursorPosition = Center;
			PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
			PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 1.f));
			Act.Step = 1;
			return;
		}
		if (++Act.Frames >= 2)
		{
			Finish(TEXT("ok"));
		}
		return;
	}

	// ---- マスを指して置く・回収・スタート位置（設置パートだけ）
	if (GM->GetPhase() != EKakurenboPhase::Build || IsMenuOpen())
	{
		Finish(TEXT("fail not-build-phase"));
		return;
	}
	if (!Grid->IsInside(Act.Cell))
	{
		Finish(TEXT("fail outside"));
		return;
	}
	const UKakurenboGridSubsystem* ConstGrid = Grid;
	const int32 Height = ConstGrid->GetColumnHeight(Act.Cell);
	// 狙う点：そのマスの一番上の面（回収は一番上の壁の少し下）
	const FVector Floor = Grid->CellFloorCenter(Act.Cell);
	const float TopZ = Floor.Z + Height * GM->BlockHeight + (Act.Verb == TEXT("rclick") && Height > 0 ? -GM->BlockHeight * 0.3f : 2.f);
	const FVector Aim(Floor.X, Floor.Y, TopZ);
	int32 ViewX = 0, ViewY = 0;
	GetViewportSize(ViewX, ViewY);

	switch (Act.Step)
	{
	case 0:
	{
		Act.BeforeHeight = Height;
		Act.bBeforeTrap = Grid->HasTrap(Act.Cell);
		const ATrapActor* Trap = Grid->GetTrap(Act.Cell);
		Act.BeforeDir = Trap ? Trap->GetDir() : -1;
		Act.BeforeStart = Grid->WorldToCell(Hider->GetActorLocation());
		// 画面の端・下の欄・ボタンの下に隠れるマスなら、人がするようにカメラをそのマスへ動かす
		FVector2D Screen;
		FKakurenboUIButton Over;
		const bool bOnScreen = ProjectWorldLocationToScreen(Aim, Screen, true);
		const bool bSafe = bOnScreen && Screen.X > ViewX * 0.05f && Screen.X < ViewX * 0.95f && Screen.Y > ViewY * 0.14f && Screen.Y < ViewY * 0.78f
			&& !KHUD->FindButtonAt(Screen, Over);
		if (!bSafe || Act.Tries > 0)
		{
			const FVector Focus = Hider->GetTopDownFocus();
			Hider->SetTopDownFocus(FVector(Floor.X, Floor.Y, Focus.Z));
		}
		Act.Step = 1;
		Act.Frames = 0;
		return;
	}
	case 1:
		if (++Act.Frames >= (Act.Tries > 0 ? 4 : 1))
		{
			// カーソルは画面の中だけ（本物のマウスと同じ）。カメラがまだ動いていて画面の外なら、少し待つ
			FVector2D Screen;
			const bool bOnScreen = ProjectWorldLocationToScreen(Aim, Screen, true) && Screen.X >= 0.f && Screen.Y >= 0.f && Screen.X < ViewX && Screen.Y < ViewY;
			if (!bOnScreen && Act.Frames < 12)
			{
				return;
			}
			if (bOnScreen)
			{
				bUseTestCursor = true;
				TestCursorPosition = Screen;
			}
			Act.Step = 2;
			Act.Frames = 0;
		}
		return;
	case 2:
	{
		// 次のフレームの設置パートの入力で、カーソルの先のマスが決まっている
		if (++Act.Frames < 2)
		{
			return;
		}
		bool bOnTarget = false;
		if (Act.Verb == TEXT("rclick") && (Act.BeforeHeight > 0 || Act.bBeforeTrap))
		{
			bOnTarget = (PickUpTarget && ConstGrid->WorldToCell(PickUpTarget->GetActorLocation()) == Act.Cell) || (bHasTrapPickUpTarget && TrapPickUpCell == Act.Cell);
		}
		else if (Act.Verb == TEXT("place") && IsRotatableTrapAt(Act.Cell))
		{
			bOnTarget = bHasTrapPickUpTarget && TrapPickUpCell == Act.Cell;
		}
		else
		{
			bOnTarget = bHasBuildTarget && BuildTargetCell == Act.Cell;
		}
		if (!bOnTarget && Act.Tries < 2)
		{
			++Act.Tries;
			Act.Step = 0;
			return;
		}
		Act.Reason = Act.Verb == TEXT("rclick") ? FString(TEXT("nothing-to-pick-up")) : BuildTargetReason.ToString();
		if (bOnTarget)
		{
			if (Act.Verb == TEXT("rclick"))
			{
				PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::RightMouseButton, IE_Pressed, 1.f));
				PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::RightMouseButton, IE_Released, 1.f));
			}
			else if (Act.Verb == TEXT("setstart"))
			{
				PlaytestTap(EKakurenboAction::SetStart);
			}
			else
			{
				PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
				PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 1.f));
			}
		}
		else
		{
			// マウスのカーソルでそのマスを指せなかった（レイが見えない物に当たる・ずれる）：記録して、同じ働きの関数で行う
			Act.bFallback = true;
			AgentBug(FString::Printf(TEXT("cursor-miss:%s:%d,%d"), *Act.Verb, Act.Cell.X, Act.Cell.Y),
				FString::Printf(TEXT("cursor-miss verb=%s cell=%d,%d target=%s"), *Act.Verb, Act.Cell.X, Act.Cell.Y,
					bHasBuildTarget ? *FString::Printf(TEXT("%d,%d"), BuildTargetCell.X, BuildTargetCell.Y) : TEXT("none")), true);
			UpdateBuildTargetAtCell(Act.Cell);
			Act.Reason = Act.Verb == TEXT("rclick") ? FString(TEXT("nothing-to-pick-up")) : BuildTargetReason.ToString();
			if (Act.Verb == TEXT("rclick"))
			{
				if (APlaceableBlock* Top = Grid->GetTopBlock(Act.Cell)) { GM->PickUpWall(Top); }
				else { GM->PickUpTrap(Act.Cell); }
			}
			else if (Act.Verb == TEXT("setstart"))
			{
				SetStartCell(Act.Cell);
			}
			else if (IsRotatableTrapAt(Act.Cell))
			{
				GM->RotateTrap(Act.Cell);
			}
			else if (bCanPlaceAtTarget)
			{
				if (IsTrapSlotSelected()) { GM->PlaceTrap(Act.Cell, GetSelectedTrapType()); }
				else { GM->PlaceWall(Act.Cell, GetSelectedWallType()); }
			}
		}
		Act.Step = 3;
		Act.Frames = 0;
		return;
	}
	default:
	{
		if (++Act.Frames < 2)
		{
			return;
		}
		// 結果を確かめる
		const int32 NewHeight = ConstGrid->GetColumnHeight(Act.Cell);
		const bool bNewTrap = Grid->HasTrap(Act.Cell);
		const ATrapActor* Trap = Grid->GetTrap(Act.Cell);
		const int32 NewDir = Trap ? Trap->GetDir() : -1;
		bool bChanged = false;
		if (Act.Verb == TEXT("rclick"))
		{
			bChanged = NewHeight < Act.BeforeHeight || (Act.bBeforeTrap && !bNewTrap);
		}
		else if (Act.Verb == TEXT("setstart"))
		{
			bChanged = Grid->WorldToCell(Hider->GetActorLocation()) == Act.Cell;
		}
		else
		{
			bChanged = NewHeight > Act.BeforeHeight || (bNewTrap && !Act.bBeforeTrap) || (NewDir != Act.BeforeDir && Act.BeforeDir >= 0);
		}
		if (!bChanged && Act.Verb == TEXT("setstart"))
		{
			// スタート位置を動かせない理由は画面上のお知らせに出ている
			if (const AKakurenboGameState* GState = GetWorld()->GetGameState<AKakurenboGameState>())
			{
				Act.Reason = GState->NoticeText.ToString();
			}
		}
		const FString Reason = AgentClean(Act.Reason).Replace(TEXT(" "), TEXT("_"));
		Finish(bChanged ? (Act.bFallback ? TEXT("ok fallback") : TEXT("ok")) : FString::Printf(TEXT("fail %s"), Reason.IsEmpty() ? TEXT("no-change") : *Reason));
		return;
	}
	}
}

void AKakurenboPlayerController::AgentTickBody(float RealDeltaSeconds)
{
	FKakurenboAgentBridge& A = *Agent;
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	AHiderCharacter* Hider = GetHider();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const bool bHide = GM && Hider && Grid && GM->GetPhase() == EKakurenboPhase::Hide && !IsMenuOpen() && !IsPaused();

	auto ReleaseForward = [this, &A]()
	{
		if (A.bForwardDown)
		{
			PlaytestKey(EKakurenboAction::MoveForward, false);
			A.bForwardDown = false;
		}
	};
	if (!bHide)
	{
		ReleaseForward();
		if (A.bWalking)
		{
			A.bWalking = false;
			A.WalkState = TEXT("idle");
		}
		if (GM && GM->GetPhase() != EKakurenboPhase::Hide)
		{
			A.MashRate = 0.f; // 次のかくれんぼは連打しないところから
			A.WantCrouch = -1;
		}
		return;
	}
	const double GameNow = GetWorld()->GetTimeSeconds();

	// しゃがむ（1 回押して切り替えの設定なら、今の状態と違うときだけ押す）
	if (A.WantCrouch >= 0 && A.Clock >= A.NextCrouchTap)
	{
		const bool bWant = A.WantCrouch == 1;
		const UKakurenboSettings* Set = Settings();
		const bool bNow = (Set && Set->bCrouchToggle) ? bSneakToggled : Hider->IsSneaking();
		if (bNow != bWant)
		{
			PlaytestTap(EKakurenboAction::Sneak);
			A.NextCrouchTap = A.Clock + 0.3;
		}
	}

	// 連打（ゲームの時間で決まった回数）
	if (A.MashRate > 0.f && GM->CanMash() && GameNow >= A.NextMash)
	{
		PlaytestTap(EKakurenboAction::Mash);
		A.NextMash = GameNow + 1.0 / A.MashRate;
	}

	// 歩く：カメラを次のマスへ向けて W を押す（WASD はカメラ基準）
	if (!A.bWalking)
	{
		ReleaseForward();
		return;
	}
	if (!A.WalkPath.IsValidIndex(A.WalkIndex))
	{
		A.bWalking = false;
		A.WalkState = TEXT("arrived");
		ReleaseForward();
		return;
	}
	const FVector Target = Grid->CellFloorCenter(A.WalkPath[A.WalkIndex]);
	FVector To = Target - Hider->GetActorLocation();
	To.Z = 0.f;
	if (To.Size() < 40.f)
	{
		++A.WalkIndex;
		return;
	}
	SetControlRotation(FRotator(-20.f, To.Rotation().Yaw, 0.f));
	if (!A.bForwardDown)
	{
		PlaytestKey(EKakurenboAction::MoveForward, true);
		A.bForwardDown = true;
		A.StuckPos = Hider->GetActorLocation();
		A.StuckTime = A.Clock;
	}
	// 登る道なら、次のマスの段の手前で引っかかるのを待たずに登る（追われているときは待つ時間が惜しい）
	if (A.bWalkClimb && !Hider->IsClimbing() && A.Clock >= A.NextClimbTap && To.Size() < Grid->GetCellSize() * 0.9f)
	{
		const FIntPoint Here = Grid->WorldToCell(Hider->GetActorLocation());
		const FIntPoint Next = A.WalkPath[A.WalkIndex];
		if (Next != Here && Grid->GetColumnHeight(Next) > Grid->GetColumnHeight(Here))
		{
			PlaytestTap(EKakurenboAction::Climb);
			A.NextClimbTap = A.Clock + 0.4;
		}
	}
	// 引っかかったら：よじ登ってよければ登る・道を選び直す・あきらめる（あきらめたら不具合かもしれないので記録）
	if (A.Clock - A.StuckTime > 1.0 / FMath::Max(A.Speed, 0.25f) * 1.2)
	{
		if (FVector::Dist2D(Hider->GetActorLocation(), A.StuckPos) < 30.f && !Hider->IsClimbing())
		{
			++A.StuckTries;
			const FIntPoint MyCell = Grid->WorldToCell(Hider->GetActorLocation());
			const FIntPoint Next = A.WalkPath[A.WalkIndex];
			if (A.bWalkClimb && GM->IsClimbUnlocked() && A.StuckTries <= 2)
			{
				PlaytestTap(EKakurenboAction::Climb);
			}
			else if (A.StuckTries <= 3)
			{
				// 道を選び直す
				TArray<FIntPoint> NewPath;
				if (KakurenboPathfinding::FindPath(Grid->BuildWalkGrid(), MyCell, A.WalkGoal, NewPath) && NewPath.Num() > 0)
				{
					A.WalkPath = NewPath;
					A.WalkIndex = 0;
				}
			}
			else
			{
				A.bWalking = false;
				A.WalkState = TEXT("stuck");
				ReleaseForward();
				// 次のマスが歩けるはずなのに進めないなら、引っかかる場所（家具の当たり判定のずれなど）かもしれない
				if (Grid->IsWalkable(Next))
				{
					AgentBug(FString::Printf(TEXT("stuck-walk:%d,%d"), MyCell.X, MyCell.Y),
						FString::Printf(TEXT("stuck-walk at=%d,%d next=%d,%d goal=%d,%d map=%s"), MyCell.X, MyCell.Y, Next.X, Next.Y, A.WalkGoal.X, A.WalkGoal.Y,
							*GM->GetCurrentMapName().ToString()), true);
				}
			}
		}
		else
		{
			A.StuckTries = 0;
		}
		A.StuckPos = Hider->GetActorLocation();
		A.StuckTime = A.Clock;
	}
}

// ---------------------------------------------------------------- 不具合の検出

void AKakurenboPlayerController::AgentBug(const FString& Key, const FString& Line, bool bShot)
{
	FKakurenboAgentBridge& A = *Agent;
	if (A.BugKeys.Contains(Key) || A.BugKeys.Num() > 4000)
	{
		return;
	}
	A.BugKeys.Add(Key);
	FString Entry = AgentClean(Line);
	if (bShot && A.ShotsTaken < A.ShotBudget)
	{
		++A.ShotsTaken;
		FString Kind;
		Key.Split(TEXT(":"), &Kind, nullptr);
		const FString Path = A.ShotDir / FString::Printf(TEXT("%s_bug_%s.png"), *FDateTime::Now().ToString(TEXT("%H%M%S")), *FPaths::MakeValidFileName(Kind.IsEmpty() ? Key : Kind));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		Entry += FString::Printf(TEXT(" shot=%s"), *FPaths::GetCleanFilename(Path));
	}
	A.PendingBugs.Add(Entry);
	UE_LOG(LogTemp, Warning, TEXT("[Agent] BUG %s"), *Entry);
}

void AKakurenboPlayerController::AgentCheckBugs(float RealDeltaSeconds)
{
	FKakurenboAgentBridge& A = *Agent;
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	const AKakurenboGameState* S = GetWorld()->GetGameState<AKakurenboGameState>();
	UKakurenboGridSubsystem* Grid = GetWorld()->GetSubsystem<UKakurenboGridSubsystem>();
	const AKakurenboHUD* KHUD = GetHUD<AKakurenboHUD>();
	AHiderCharacter* Hider = GetHider();
	if (!GM || !S || !Grid || !Grid->IsConfigured() || !KHUD || !Hider)
	{
		return;
	}
	const FString MapName = GM->GetCurrentMapName().ToString();

	// かくれんぼの終わり（見つかった・逃げ切った）：触れた鬼と距離を覚えておき、遠すぎれば不具合かもしれない
	const bool bPhaseChanged = !A.bPhaseKnown || A.LastPhase != S->Phase;
	if (bPhaseChanged)
	{
		if (A.bPhaseKnown && A.LastPhase == EKakurenboPhase::Hide && S->Phase == EKakurenboPhase::Result)
		{
			if (S->bLastRoundCleared)
			{
				A.RoundEndEvent = TEXT("cleared");
			}
			else
			{
				A.RoundEndEvent = FString::Printf(TEXT("caught by=%s state=%s dist=%.2f"), *A.LastNearestType, *A.LastNearestState, A.LastNearestDist / 100.f);
				if (A.LastNearestDist > 300.f)
				{
					AgentBug(FString::Printf(TEXT("caught-far:%s:%.0f"), *MapName, A.LastNearestDist / 50.f),
						FString::Printf(TEXT("caught-far nearest=%s state=%s dist=%.1fm stage=%d map=%s"), *A.LastNearestType, *A.LastNearestState, A.LastNearestDist / 100.f, S->Stage, *MapName), true);
				}
			}
		}
		A.OniWatch.Reset();
		A.OverrunSince = -1.0;
	}
	A.bPhaseKnown = true;
	A.LastPhase = S->Phase;

	// 数の値がおかしくないか
	if (!FMath::IsFinite(S->Coins) || S->Coins < -0.5)
	{
		AgentBug(TEXT("bad-coins"), FString::Printf(TEXT("bad-coins value=%f stage=%d"), S->Coins, S->Stage), false);
	}
	for (int32 I = 0; I < S->WallStock.Num(); ++I)
	{
		if (S->WallStock[I] < 0)
		{
			AgentBug(FString::Printf(TEXT("bad-stock:wall%d"), I), FString::Printf(TEXT("bad-stock wall=%d value=%d"), I, S->WallStock[I]), false);
		}
	}
	for (int32 I = 0; I < S->TrapStock.Num(); ++I)
	{
		if (S->TrapStock[I] < 0)
		{
			AgentBug(FString::Printf(TEXT("bad-stock:trap%d"), I), FString::Printf(TEXT("bad-stock trap=%d value=%d"), I, S->TrapStock[I]), false);
		}
	}

	// 画面の文と鬼の様子は 0.5 秒ごと（鬼との距離だけは毎フレーム覚えておく。見つかった瞬間の距離を残すため）
	const bool bSlowCheck = A.Clock >= A.NextBugCheck || bPhaseChanged;
	if (bSlowCheck)
	{
		A.NextBugCheck = A.Clock + 0.5;

		// 画面の文：枠からはみ出して「…」になった・ほかの文と重なった・画面の外
		const TArray<FKakurenboDrawnText>& Layout = KHUD->GetDrawnTextLayout();
		int32 ViewX = 0, ViewY = 0;
		GetViewportSize(ViewX, ViewY);
		const FString Where = FString::Printf(TEXT("%s/%s"), AgentPhaseText(KHUD->GetDrawnPhase()), AgentMenuText(KHUD->GetDrawnMenu())); // 文を描いたときの画面
		for (int32 I = 0; I < Layout.Num(); ++I)
		{
			const FKakurenboDrawnText& T = Layout[I];
			if (T.bWorld)
			{
				continue; // ワールドの名前はカメラで動くので、画面の端で切れても重なってもよい
			}
			if (T.bTruncated)
			{
				AgentBug(TEXT("ui-truncated:") + T.Text, FString::Printf(TEXT("ui-truncated screen=%s text=\"%s\""), *Where, *T.Text), false);
			}
			if (ViewX > 0 && (T.Rect.Min.X < -2.f || T.Rect.Max.X > ViewX + 2.f || T.Rect.Min.Y < -2.f || T.Rect.Max.Y > ViewY + 2.f))
			{
				AgentBug(TEXT("ui-offscreen:") + T.Text, FString::Printf(TEXT("ui-offscreen screen=%s text=\"%s\" rect=%.0f,%.0f-%.0f,%.0f view=%dx%d"), *Where, *T.Text,
					T.Rect.Min.X, T.Rect.Min.Y, T.Rect.Max.X, T.Rect.Max.Y, ViewX, ViewY), true);
			}
			for (int32 J = I + 1; J < Layout.Num(); ++J)
			{
				const FKakurenboDrawnText& U = Layout[J];
				if (U.bWorld || T.bCursor || U.bCursor || T.bMenu != U.bMenu || T.Text == U.Text || T.Text.Contains(U.Text) || U.Text.Contains(T.Text) || !T.Rect.Intersect(U.Rect))
				{
					continue;
				}
				const FBox2D Both(FVector2D(FMath::Max(T.Rect.Min.X, U.Rect.Min.X), FMath::Max(T.Rect.Min.Y, U.Rect.Min.Y)),
					FVector2D(FMath::Min(T.Rect.Max.X, U.Rect.Max.X), FMath::Min(T.Rect.Max.Y, U.Rect.Max.Y)));
				const double Area = FMath::Max(0.0, Both.Max.X - Both.Min.X) * FMath::Max(0.0, Both.Max.Y - Both.Min.Y);
				const double Smaller = FMath::Min(T.Rect.GetArea(), U.Rect.GetArea());
				if (Smaller > 1.0 && Area > Smaller * 0.3)
				{
					const FString Key = T.Text < U.Text ? T.Text + TEXT("|") + U.Text : U.Text + TEXT("|") + T.Text;
					AgentBug(TEXT("ui-overlap:") + Key, FString::Printf(TEXT("ui-overlap screen=%s a=\"%s\" b=\"%s\""), *Where, *T.Text, *U.Text), true);
				}
			}
		}
	}

	if (S->Phase != EKakurenboPhase::Hide || IsPaused())
	{
		return;
	}
	const FVector Me = Hider->GetActorLocation();
	const FIntPoint MyCell = Grid->WorldToCell(Me);
	const float FloorZ = Grid->CellFloorCenter(FIntPoint(0, 0)).Z;

	// 一番近い鬼（見つかった瞬間の記録）
	float Nearest = 1e9f;
	for (const AOniCharacter* Oni : GM->GetOnis())
	{
		if (!Oni || !Oni->IsActive())
		{
			continue;
		}
		const float D = FVector::Dist2D(Oni->GetActorLocation(), Me);
		if (D < Nearest)
		{
			Nearest = D;
			A.LastNearestType = AgentEnumShort(UEnum::GetValueAsString(Oni->OniType));
			A.LastNearestState = AgentEnumShort(UEnum::GetValueAsString(Oni->GetOniState()));
			A.LastNearestDist = D;
		}
	}

	if (!bSlowCheck)
	{
		return;
	}

	// 自分：落ちた・館の外に出た
	if (Me.Z < FloorZ - 300.f || !Grid->IsInside(MyCell))
	{
		AgentBug(FString::Printf(TEXT("player-out:%s"), *MapName), FString::Printf(TEXT("player-out pos=%.0f,%.0f,%.0f cell=%d,%d map=%s"), Me.X, Me.Y, Me.Z, MyCell.X, MyCell.Y, *MapName), true);
	}
	// 時間が 0 になったのにかくれんぼが終わらない
	if (S->HideTimeRemaining <= 0.f && S->HideStartCountdown <= 0.f)
	{
		if (A.OverrunSince < 0.0)
		{
			A.OverrunSince = A.Clock;
		}
		else if (A.Clock - A.OverrunSince > 3.0)
		{
			AgentBug(FString::Printf(TEXT("time-overrun:%d"), S->Stage), FString::Printf(TEXT("time-overrun stage=%d map=%s"), S->Stage, *MapName), true);
		}
	}
	// 鬼：館の外に出た・歩くはずなのに長い間動かない
	const double GameNow = GetWorld()->GetTimeSeconds();
	for (AOniCharacter* Oni : GM->GetOnis())
	{
		if (!Oni || !Oni->IsActive())
		{
			continue;
		}
		const FVector P = Oni->GetActorLocation();
		const FIntPoint Cell = Grid->WorldToCell(P);
		const FString Type = AgentEnumShort(UEnum::GetValueAsString(Oni->OniType));
		const FString State = AgentEnumShort(UEnum::GetValueAsString(Oni->GetOniState()));
		if (P.Z < FloorZ - 300.f || !Grid->IsInside(Cell))
		{
			AgentBug(FString::Printf(TEXT("oni-out:%s:%s"), *Type, *MapName), FString::Printf(TEXT("oni-out type=%s state=%s pos=%.0f,%.0f,%.0f map=%s"), *Type, *State, P.X, P.Y, P.Z, *MapName), true);
		}
		const EOniState St = Oni->GetOniState();
		const bool bShouldMove = Oni->OniType != EOniType::Detector && !Oni->IsDarumaFrozen() && !Oni->IsPettingCat()
			&& (St == EOniState::Wander || St == EOniState::Investigate || St == EOniState::Chase);
		FKakurenboAgentBridge::FOniWatch& W = A.OniWatch.FindOrAdd(Oni);
		if (!bShouldMove || FVector::Dist2D(W.Pos, P) > 40.f)
		{
			W.Pos = P;
			W.Since = GameNow;
		}
		else if (GameNow - W.Since > 20.0)
		{
			AgentBug(FString::Printf(TEXT("oni-stuck:%s:%d,%d"), *MapName, Cell.X, Cell.Y),
				FString::Printf(TEXT("oni-stuck type=%s state=%s cell=%d,%d for=%.0fs map=%s stage=%d"), *Type, *State, Cell.X, Cell.Y, GameNow - W.Since, *MapName, S->Stage), true);
			W.Since = GameNow; // 同じ場所ならもう伝えない（Key が同じ）
		}
	}
}

// ---------------------------------------------------------------- 見えているもの

FString AKakurenboPlayerController::AgentBuildObservation()
{
	FKakurenboAgentBridge& A = *Agent;
	UWorld* World = GetWorld();
	AKakurenboGameMode* GM = GetKakurenboGameMode();
	const AKakurenboGameState* S = World->GetGameState<AKakurenboGameState>();
	UKakurenboGridSubsystem* Grid = World->GetSubsystem<UKakurenboGridSubsystem>();
	const AKakurenboHUD* KHUD = GetHUD<AKakurenboHUD>();
	AHiderCharacter* Hider = GetHider();

	FString Out;
	Out.Reserve(16384);
	Out += FString::Printf(TEXT("#OBS %d t=%.2f rt=%.2f\n"), ++A.Seq, World->GetTimeSeconds(), A.Clock);
	if (!GM || !S || !Grid || !Grid->IsConfigured() || !KHUD)
	{
		Out += TEXT("PHASE Loading\n#END\n");
		return Out;
	}
	const bool bPhaseChanged = !A.bObsPhaseKnown || A.ObsPhase != S->Phase;
	A.bObsPhaseKnown = true;
	A.ObsPhase = S->Phase;
	Out += FString::Printf(TEXT("PHASE %s\n"), AgentPhaseText(S->Phase));
	Out += FString::Printf(TEXT("MENU %s\n"), AgentMenuText(GetMenu()));
	for (const FString& Ack : A.Acks)
	{
		Out += TEXT("ACK ") + Ack + TEXT("\n");
	}
	A.Acks.Reset();

	// ---- 画面の文（上から、左から）
	{
		TArray<FKakurenboDrawnText> Layout = KHUD->GetDrawnTextLayout();
		Layout.StableSort([](const FKakurenboDrawnText& L, const FKakurenboDrawnText& R)
		{
			const int32 RowL = FMath::FloorToInt(L.Rect.Min.Y / 12.f);
			const int32 RowR = FMath::FloorToInt(R.Rect.Min.Y / 12.f);
			return RowL != RowR ? RowL < RowR : L.Rect.Min.X < R.Rect.Min.X;
		});
		Out += TEXT("SCREEN\n");
		TSet<FString> Seen;
		for (const FKakurenboDrawnText& T : Layout)
		{
			if (T.bWorld)
			{
				continue; // ワールドの名前は LABELS に場所付きで出す
			}
			const FString Line = AgentClean(T.Text);
			const FString Key = FString::Printf(TEXT("%s@%d,%d"), *Line, FMath::RoundToInt(T.Rect.Min.X / 8.f), FMath::RoundToInt(T.Rect.Min.Y / 8.f));
			if (Line.IsEmpty() || Seen.Contains(Key))
			{
				continue;
			}
			Seen.Add(Key);
			Out += FString::Printf(TEXT("> %s%d %s\n"), T.bMenu ? TEXT("m") : TEXT("s"), T.Size, *Line); // m = メニュー（暗くした画面の上）の文
		}
	}

	// ---- 押せるボタン（前のフレームに描いたもの。画面が変わった直後は無い）
	Out += TEXT("BUTTONS\n");
	if (KHUD->AreButtonsFor(S->Phase, GetMenu()))
	{
		for (const FKakurenboUIButton& B : KHUD->GetButtons())
		{
			FString Label = B.Label;
			if (Label.IsEmpty())
			{
				for (const FKakurenboDrawnText& T : KHUD->GetDrawnTextLayout())
				{
					if (!T.bWorld && B.Rect.IsInside(T.Rect.GetCenter()))
					{
						Label += (Label.IsEmpty() ? TEXT("") : TEXT(" / ")) + AgentClean(T.Text);
					}
				}
			}
			Out += FString::Printf(TEXT("> %s.%d | %s\n"), AgentUIActionText(B.Action), B.Index, *AgentClean(Label));
		}
	}

	// ---- マスの座標（整数がマスの真ん中）
	const FVector Origin = Grid->CellFloorCenter(FIntPoint(0, 0));
	const float CellSize = Grid->GetCellSize();
	auto ToCell = [&Origin, CellSize](const FVector& W) { return FVector2D((W.X - Origin.X) / CellSize, (W.Y - Origin.Y) / CellSize); };

	Out += TEXT("LABELS\n");
	for (const FKakurenboWorldLabel& L : KHUD->GetWorldLabels())
	{
		const FVector2D C = ToCell(L.World);
		Out += FString::Printf(TEXT("> %s @ %.1f,%.1f\n"), *AgentClean(L.Text), C.X, C.Y);
	}

	// ---- 自分の体
	if (Hider)
	{
		const FVector L = Hider->GetActorLocation();
		const FVector2D C = ToCell(L);
		const float Feet = L.Z - Hider->GetSimpleCollisionHalfHeight();
		Out += FString::Printf(TEXT("SELF pos=%.2f,%.2f h=%.2f cam=%.0f body=%.0f crouch=%d climbing=%d dashing=%d\n"), C.X, C.Y, (Feet - Origin.Z) / FMath::Max(GM->BlockHeight, 1.f),
			FRotator::ClampAxis(GetControlRotation().Yaw), FRotator::ClampAxis(Hider->GetActorRotation().Yaw), Hider->IsSneaking() ? 1 : 0, Hider->IsClimbing() ? 1 : 0, Hider->IsDashing() ? 1 : 0);
	}
	Out += FString::Printf(TEXT("WALK state=%s goal=%d,%d left=%d mash=%.1f\n"), *A.WalkState, A.WalkGoal.X, A.WalkGoal.Y,
		A.bWalking ? FMath::Max(0, A.WalkPath.Num() - A.WalkIndex) : 0, A.MashRate);
	// 文字ではない画面の目盛り（煙幕の残りは四角の数で出ている）を文字にする
	if (S->Phase == EKakurenboPhase::Hide && Hider && Hider->bDashUnlocked)
	{
		Out += FString::Printf(TEXT("GAUGE 煙幕 left=%d max=%d\n"), Hider->GetDashUsesLeft(), FMath::Max(1, Hider->DashUsesPerRound));
	}

	// ---- 見えている鬼・お宝（かくれんぼ中。壁・家具にさえぎられず、明るさの届く所）と聞こえた音
	if (S->Phase == EKakurenboPhase::Hide && Hider)
	{
		const FVector Eye = Hider->GetPawnViewLocation();
		FVector CamLoc;
		FRotator CamRot;
		GetPlayerViewPoint(CamLoc, CamRot);
		auto CanSee = [&](const AActor* Target, const FVector& Point) -> bool
		{
			float Range = 3500.f;
			if (GM->IsMansionDark())
			{
				// 真っ暗：懐中電灯の照らす前だけ少し遠くまで、それ以外はすぐそばだけ
				const FVector Dir = (Point - Eye).GetSafeNormal2D();
				Range = FVector::DotProduct(CamRot.Vector().GetSafeNormal2D(), Dir) > 0.8f ? 900.f : 300.f;
			}
			if (FVector::Dist(Eye, Point) > Range)
			{
				return false;
			}
			FCollisionQueryParams Params(SCENE_QUERY_STAT(KakuAgentSight), false);
			Params.AddIgnoredActor(Hider);
			if (Target)
			{
				Params.AddIgnoredActor(Target);
			}
			for (const AOniCharacter* Oni : GM->GetOnis())
			{
				Params.AddIgnoredActor(Oni);
			}
			return !World->LineTraceTestByChannel(Eye, Point, ECC_Visibility, Params) || !World->LineTraceTestByChannel(CamLoc, Point, ECC_Visibility, Params);
		};
		const FVector2D Me = ToCell(Hider->GetActorLocation());
		for (const AOniCharacter* Oni : GM->GetOnis())
		{
			if (!Oni || !Oni->IsActive() || !CanSee(Oni, Oni->GetActorLocation() + FVector(0.f, 0.f, 40.f)))
			{
				continue;
			}
			const FVector2D C = ToCell(Oni->GetActorLocation());
			const FVector2D V = FVector2D(Oni->GetVelocity().X, Oni->GetVelocity().Y) / CellSize;
			const EOniState St = Oni->GetOniState();
			const TCHAR* Cue = Oni->IsDarumaFrozen() ? TEXT("frozen") : Oni->IsPettingCat() ? TEXT("petting") : St == EOniState::Chase ? TEXT("red-eyes")
				: St == EOniState::Stunned ? TEXT("stuck") : St == EOniState::Attack ? TEXT("hitting") : TEXT("-");
			const FText Name = GM->GetOniTypeRow(Oni->OniType).DisplayName;
			Out += FString::Printf(TEXT("SEE oni name=%s pos=%.1f,%.1f vel=%.1f,%.1f dist=%.1f cue=%s\n"), *AgentClean(Name.ToString()).Replace(TEXT(" "), TEXT("_")),
				C.X, C.Y, V.X, V.Y, FVector2D::Distance(C, Me), Cue);
		}
		for (const ATreasureActor* T : GM->GetTreasures())
		{
			if (T && CanSee(T, T->GetActorLocation() + FVector(0.f, 0.f, 30.f)))
			{
				const FVector2D C = ToCell(T->GetActorLocation());
				Out += FString::Printf(TEXT("SEE treasure pos=%.1f,%.1f dist=%.1f\n"), C.X, C.Y, FVector2D::Distance(C, Me));
			}
		}
		// 床に広がった輪（前の観測から後に出たもの）。自分のそば（2 マス以内）の輪と、輪の真ん中が見える輪だけ
		if (UKakurenboFxSubsystem* Fx = World->GetSubsystem<UKakurenboFxSubsystem>())
		{
			for (const FKakurenboShownRing& R : Fx->ShownRings)
			{
				const FVector2D C = ToCell(R.Location);
				const FVector Floor(R.Location.X, R.Location.Y, Origin.Z + 20.f);
				if (FVector2D::Distance(C, Me) > 2.f && !CanSee(nullptr, Floor))
				{
					continue;
				}
				Out += FString::Printf(TEXT("SEE ring pos=%.1f,%.1f r=%.1f color=%s w=%.0f dist=%.1f\n"), C.X, C.Y, R.Radius / CellSize,
					AgentColorName(R.Color), R.Thickness, FVector2D::Distance(C, Me));
			}
		}
		// 聞こえた音：音の種類と方角ごとに一番大きいもの
		if (UKakurenboSoundSubsystem* Sound = World->GetSubsystem<UKakurenboSoundSubsystem>())
		{
			TMap<FString, float> Loudest;
			TMap<FString, bool> Muffled;
			for (const FKakurenboHeardSound& H : Sound->Heard)
			{
				const FString Name = AgentEnumShort(UEnum::GetValueAsString(H.Sfx));
				FString Dir = TEXT("-");
				if (H.bSpatial)
				{
					// 方角は 8 方位（dir）と、人の耳くらいの細かさ（15 度刻み。deg：0 = E（+X）、90 = S（+Y））
					const FVector2D C = ToCell(H.Location);
					const FVector2D D = C - Me;
					if (D.Size() < 0.6f)
					{
						Dir = TEXT("here");
					}
					else
					{
						const int32 Deg = ((FMath::RoundToInt(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) / 15.f) * 15) % 360 + 360) % 360;
						Dir = FString::Printf(TEXT("%s deg=%d"), AgentDirText(D.X, D.Y), Deg);
					}
				}
				const FString Key = Name + TEXT(" dir=") + Dir;
				float& Max = Loudest.FindOrAdd(Key);
				Max = FMath::Max(Max, H.Loudness);
				Muffled.FindOrAdd(Key) |= H.bOccluded;
			}
			for (const TPair<FString, float>& P : Loudest)
			{
				Out += FString::Printf(TEXT("HEAR %s loud=%.2f muffled=%d\n"), *P.Key, P.Value, Muffled.FindRef(P.Key) ? 1 : 0);
			}
		}
	}
	if (UKakurenboSoundSubsystem* Sound = World->GetSubsystem<UKakurenboSoundSubsystem>())
	{
		Sound->Heard.Reset();
	}
	if (UKakurenboFxSubsystem* Fx = World->GetSubsystem<UKakurenboFxSubsystem>())
	{
		Fx->ShownRings.Reset();
	}

	// ---- 上から見た館の形（家具の高さは館が変わったときに上からレイを落として測る）
	const FIntPoint Size(Grid->GetSizeX(), Grid->GetSizeY());
	if (A.HeightMap != GM->GetCurrentMapName() || A.HeightSize != Size)
	{
		A.HeightMap = GM->GetCurrentMapName();
		A.HeightSize = Size;
		A.ObstacleHeight.Init(0.f, Size.X * Size.Y);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(KakuAgentHeight), false);
		if (Hider) { Params.AddIgnoredActor(Hider); }
		const float Quarter = CellSize * 0.3f;
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				const FIntPoint C(X, Y);
				if (!Grid->IsObstacle(C))
				{
					continue;
				}
				const FVector Center = Grid->CellFloorCenter(C);
				float Top = 0.f;
				for (const FVector2D& Off : { FVector2D(0.f, 0.f), FVector2D(Quarter, Quarter), FVector2D(-Quarter, Quarter), FVector2D(Quarter, -Quarter), FVector2D(-Quarter, -Quarter) })
				{
					FHitResult Hit;
					const FVector From(Center.X + Off.X, Center.Y + Off.Y, Center.Z + 2000.f);
					const FVector To(Center.X + Off.X, Center.Y + Off.Y, Center.Z - 5.f);
					if (World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
					{
						Top = FMath::Max(Top, Hit.ImpactPoint.Z - Center.Z);
					}
				}
				A.ObstacleHeight[Y * Size.X + X] = Top;
			}
		}
		A.bSendMap = true;
	}
	FString MapText = FString::Printf(TEXT("MAP %d %d\n"), Size.X, Size.Y);
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		FString Row = TEXT("> ");
		for (int32 X = 0; X < Size.X; ++X)
		{
			const FIntPoint C(X, Y);
			TCHAR Ch = TEXT('.');
			if (Grid->IsObstacle(C))
			{
				const float H = A.ObstacleHeight.IsValidIndex(Y * Size.X + X) ? A.ObstacleHeight[Y * Size.X + X] : 0.f;
				Ch = H <= 110.f ? TEXT('1') : H <= 175.f ? TEXT('2') : H <= 340.f ? TEXT('3') : TEXT('#');
			}
			else if (Grid->IsReservedCell(C))
			{
				Ch = TEXT('g');
			}
			Row.AppendChar(Ch);
		}
		MapText += Row + TEXT("\n");
	}
	if (A.bSendMap || bPhaseChanged || MapText != A.LastMapText)
	{
		Out += MapText;
		A.LastMapText = MapText;
		A.bSendMap = false;
	}

	// ---- 置いてある壁・道具
	{
		FString Placed = TEXT("PLACED\n");
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				const FIntPoint C(X, Y);
				const int32 Live = Grid->GetColumnHeight(C);
				const int32 Plan = Grid->GetDesignHeight(C);
				if (Live > 0 || Plan > 0)
				{
					const APlaceableBlock* Top = Grid->GetTopBlock(C);
					const int32 Type = Top ? Top->WallTypeIndex : INDEX_NONE;
					const FString Name = GM->WallTypes.IsValidIndex(Type) ? GM->WallTypes[Type].DisplayName.ToString() : TEXT("-");
					Placed += FString::Printf(TEXT("> wall %d,%d h=%d plan=%d name=%s\n"), X, Y, Live, Plan, *AgentClean(Name).Replace(TEXT(" "), TEXT("_")));
				}
				if (Grid->HasTrap(C))
				{
					const ATrapActor* Trap = Grid->GetTrap(C);
					const int32 Type = Trap ? Trap->TrapTypeIndex : Grid->GetTrapDesign(C);
					const FString Name = GM->TrapTypes.IsValidIndex(Type) ? GM->TrapTypes[Type].DisplayName.ToString() : TEXT("-");
					Placed += FString::Printf(TEXT("> item %d,%d name=%s dir=%d live=%d\n"), X, Y, *AgentClean(Name).Replace(TEXT(" "), TEXT("_")), Trap ? Trap->GetDir() : -1, Trap ? 1 : 0);
				}
			}
		}
		if (Placed != A.LastPlacedText || A.Clock >= A.NextPlacedResend || bPhaseChanged)
		{
			Out += Placed;
			A.LastPlacedText = Placed;
			A.NextPlacedResend = A.Clock + 4.0;
		}
	}

	// ---- 記録用の本当の値（AI の判断には使わない。報告書・不具合の検出用）
	Out += FString::Printf(TEXT("TEL phase=%s stage=%d prestige=%d pp=%d tpp=%d coins=%.0f gems=%d map=%s limit=%.1f remain=%.1f countdown=%.1f cleared=%d earned=%.0f mashcoins=%.0f timecoins=%.0f clearreward=%.0f mashes=%d treasures=%d/%d gemsround=%d broken=%d traps=%d summons=%d dark=%d canprestige=%d ppreset=%d\n"),
		AgentPhaseText(S->Phase), S->Stage, S->PrestigeCount, S->PrestigePoints, S->TotalPrestigePoints, S->Coins, S->Gems, *GM->GetCurrentMapName().ToString(),
		S->HideTimeLimit, S->HideTimeRemaining, S->HideStartCountdown, S->bLastRoundCleared ? 1 : 0, S->CoinsEarnedThisRound, S->MashCoinsThisRound, S->TimeCoinsThisRound,
		S->LastClearReward, S->MashCountThisRound, S->TreasuresCollectedThisRound, S->TreasuresThisRound, S->GemsThisRound, S->LastRoundWallsDestroyed,
		S->TrapsTriggeredThisRound, S->SummonsThisRound, GM->IsMansionDark() ? 1 : 0, GM->CanPrestige() ? 1 : 0, GM->GetPrestigePointsOnReset());
	if (S->Phase == EKakurenboPhase::Hide || S->Phase == EKakurenboPhase::Result)
	{
		// 宝石のお店の道具の効き目（本当の値）：ゴールドラッシュ中か・だるまさんで止まっている鬼の数
		Out += FString::Printf(TEXT("TEL gadgets goldrush=%d frozen=%d\n"), GM->IsGoldRushActive() ? 1 : 0, GM->GetDarumaFrozenCount());
		for (const AOniCharacter* Oni : GM->GetOnis())
		{
			if (Oni && Oni->IsActive())
			{
				const FVector2D C = ToCell(Oni->GetActorLocation());
				// cue：猫をなでている・身代わり人形を追っている・だるまさんで止まっている
				const TCHAR* Cue = Oni->IsDarumaFrozen() ? TEXT("frozen") : Oni->IsPettingCat() ? TEXT("petting")
					: (Oni->GetIntent() == EOniState::Chase && !Oni->IsChasingPlayer()) ? TEXT("doll") : TEXT("-");
				Out += FString::Printf(TEXT("TEL oni type=%s state=%s pos=%.1f,%.1f cue=%s\n"), *AgentEnumShort(UEnum::GetValueAsString(Oni->OniType)),
					*AgentEnumShort(UEnum::GetValueAsString(Oni->GetOniState())), C.X, C.Y, Cue);
			}
		}
	}
	if (!A.RoundEndEvent.IsEmpty())
	{
		Out += TEXT("TEL roundend ") + A.RoundEndEvent + TEXT("\n");
		A.RoundEndEvent.Reset();
	}
	for (const FString& Bug : A.PendingBugs)
	{
		Out += TEXT("BUG ") + Bug + TEXT("\n");
	}
	A.PendingBugs.Reset();
	Out += TEXT("#END\n");
	return Out;
}
