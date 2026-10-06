#include "KakurenboGameState.h"

void AKakurenboGameState::AddCoins(double Amount, bool bCountAsEarned)
{
	Coins += Amount;
	if (bCountAsEarned)
	{
		CoinsEarnedThisRound += Amount;
	}
}
