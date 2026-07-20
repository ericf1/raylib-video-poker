/*
Video Poker (Jacks or Better) - skeleton

Cards are drawn with primitives and sound effects are synthesized at startup,
so the game needs no asset files.
*/

#include "raylib.h"

#include <math.h>
#include <time.h>

#define SCREEN_WIDTH     1100
#define SCREEN_HEIGHT    720
#define SAMPLE_RATE      44100
#define STARTING_CREDITS 100
#define MAX_BET          5

#define HAND_SIZE  5
#define DECK_SIZE  52
#define RANK_COUNT 13
#define SUIT_COUNT 4

#define CARD_WIDTH     140
#define CARD_HEIGHT    198
#define CARD_SPACING   24
#define CARD_TOP       292
#define CARD_ROUNDNESS 0.10f
#define CORNER_FONT    30

// Vertical anchors. The layout is hardcoded pixels, so resizing the window
// means retuning these: the paytable's last row must clear CARD_TOP, and the
// text rows below must clear the HELD badges that hang under the cards.
#define TITLE_Y        32
#define PAYTABLE_Y     66
#define PAYTABLE_PITCH 24
#define WARNING_Y      556
#define STATUS_Y       600
#define FOOTER_Y       (SCREEN_HEIGHT - 56)
#define SCREEN_MARGIN  40

#define VOLUME_STEPS   10
#define VOLUME_DEFAULT 7

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const Color TABLE_FELT     = { 12, 68, 44, 255 };
static const Color CARD_FACE      = { 250, 249, 246, 255 };
static const Color CARD_EDGE      = { 188, 188, 194, 255 };
static const Color CARD_SHADOW    = { 0, 0, 0, 70 };
static const Color CARD_RED       = { 200, 30, 45, 255 };
static const Color CARD_BLACK     = { 25, 25, 30, 255 };
static const Color CARD_BACK      = { 32, 48, 104, 255 };
static const Color CARD_BACK_EDGE = { 92, 112, 182, 255 };
static const Color CARD_BACK_LINE = { 62, 80, 146, 255 };

// rank 0..12 maps to 2..A
typedef struct Card
{
	int rank;
	int suit;
} Card;

// order matters: ShuffleDeck derives the suit from the deck index
typedef enum Suit { SUIT_SPADES, SUIT_HEARTS, SUIT_DIAMONDS, SUIT_CLUBS } Suit;

typedef enum GameState
{
	STATE_BET,		// waiting for the player to deal
	STATE_DRAW,		// player is picking holds
	STATE_RESULT	// hand has been scored
} GameState;

typedef enum HandRank
{
	HAND_NOTHING = 0,
	HAND_JACKS_OR_BETTER,
	HAND_TWO_PAIR,
	HAND_THREE_OF_A_KIND,
	HAND_STRAIGHT,
	HAND_FLUSH,
	HAND_FULL_HOUSE,
	HAND_FOUR_OF_A_KIND,
	HAND_STRAIGHT_FLUSH,
	HAND_ROYAL_FLUSH,
	HAND_COUNT
} HandRank;

// handNames and payouts are indexed by HandRank; keep all three in sync
static const char *handNames[HAND_COUNT] = {
	"", "Jacks or Better", "Two Pair", "Three of a Kind", "Straight",
	"Flush", "Full House", "Four of a Kind", "Straight Flush", "Royal Flush"
};

// payout per credit bet (9/6 Jacks or Better)
static const int payouts[HAND_COUNT] = { 0, 1, 2, 3, 4, 6, 9, 25, 50, 250 };

static const char *rankNames[RANK_COUNT] = {
	"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"
};

typedef enum SoundId { SND_DEAL, SND_HOLD, SND_WIN, SND_LOSE, SND_COUNT } SoundId;

static Sound sounds[SND_COUNT];

static int volumeLevel = VOLUME_DEFAULT;	// 0..VOLUME_STEPS
static bool muted = false;
static bool draggingVolume = false;

static Card deck[DECK_SIZE];
static int deckTop;			// next card to be dealt

static Card hand[HAND_SIZE];
static bool held[HAND_SIZE];

static GameState state = STATE_BET;
static int credits = STARTING_CREDITS;
static int bet = MAX_BET;
static int lastWin = 0;
static HandRank lastRank = HAND_NOTHING;

// ---------------------------------------------------------------- audio

typedef struct Note
{
	float freq;			// Hz
	float duration;		// seconds
} Note;

// Each note gets a short attack and a long release; without them the
// discontinuity at note boundaries is audible as a click.
static Sound GenerateSound(const Note *notes, int noteCount, float volume, bool square)
{
	int totalFrames = 0;
	for (int i = 0; i < noteCount; i++) totalFrames += (int)(notes[i].duration * SAMPLE_RATE);

	short *samples = (short *)MemAlloc((unsigned int)totalFrames * (unsigned int)sizeof(short));
	int cursor = 0;

	for (int n = 0; n < noteCount; n++)
	{
		int frames = (int)(notes[n].duration * SAMPLE_RATE);
		int attack = SAMPLE_RATE / 500;		// 2ms
		int release = frames / 4;

		for (int f = 0; f < frames; f++)
		{
			float phase = notes[n].freq * ((float)f / SAMPLE_RATE);
			float value = square ? (((phase - floorf(phase)) < 0.5f) ? 1.0f : -1.0f)
								 : sinf(2.0f * PI * phase);

			float env = 1.0f;
			if (f < attack) env = (float)f / (float)attack;
			else if (f > frames - release) env = (float)(frames - f) / (float)release;

			samples[cursor + f] = (short)(value * env * volume * 32767.0f);
		}

		cursor += frames;
	}

	Wave wave = { 0 };
	wave.frameCount = (unsigned int)totalFrames;
	wave.sampleRate = SAMPLE_RATE;
	wave.sampleSize = 16;
	wave.channels = 1;
	wave.data = samples;

	Sound sound = LoadSoundFromWave(wave);
	UnloadWave(wave);	// LoadSoundFromWave copies the samples, so this frees ours
	return sound;
}

static void LoadSounds(void)
{
	static const Note deal[] = { { 440.0f, 0.05f }, { 660.0f, 0.06f } };
	static const Note hold[] = { { 880.0f, 0.04f } };
	static const Note win[]  = { { 523.0f, 0.09f }, { 659.0f, 0.09f },
								 { 784.0f, 0.09f }, { 1047.0f, 0.20f } };
	static const Note lose[] = { { 196.0f, 0.12f }, { 147.0f, 0.28f } };

	sounds[SND_DEAL] = GenerateSound(deal, COUNT_OF(deal), 0.35f, true);
	sounds[SND_HOLD] = GenerateSound(hold, COUNT_OF(hold), 0.30f, true);
	sounds[SND_WIN]  = GenerateSound(win,  COUNT_OF(win),  0.40f, false);
	sounds[SND_LOSE] = GenerateSound(lose, COUNT_OF(lose), 0.30f, true);
}

static void UnloadSounds(void)
{
	for (int i = 0; i < SND_COUNT; i++) UnloadSound(sounds[i]);
}

// ---------------------------------------------------------------- game

static void ShuffleDeck(void)
{
	for (int i = 0; i < DECK_SIZE; i++)
	{
		deck[i].rank = i % RANK_COUNT;
		deck[i].suit = i / RANK_COUNT;
	}

	// Fisher-Yates
	for (int i = DECK_SIZE - 1; i > 0; i--)
	{
		int j = GetRandomValue(0, i);
		Card tmp = deck[i];
		deck[i] = deck[j];
		deck[j] = tmp;
	}

	deckTop = 0;
}

static Card DrawCard(void)
{
	return deck[deckTop++];
}

static HandRank EvaluateHand(const Card *cards)
{
	int rankCounts[RANK_COUNT] = { 0 };
	int suitCounts[SUIT_COUNT] = { 0 };

	for (int i = 0; i < HAND_SIZE; i++)
	{
		rankCounts[cards[i].rank]++;
		suitCounts[cards[i].suit]++;
	}

	int distinct = 0, lowest = RANK_COUNT - 1, highest = 0;
	int pairs = 0, trips = 0, quads = 0;
	bool highPair = false;

	for (int i = 0; i < RANK_COUNT; i++)
	{
		if (rankCounts[i] == 0) continue;

		distinct++;
		if (i < lowest) lowest = i;
		if (i > highest) highest = i;

		if (rankCounts[i] == 2) { pairs++; if (i >= 9) highPair = true; }	// J = index 9
		else if (rankCounts[i] == 3) trips++;
		else if (rankCounts[i] == 4) quads++;
	}

	// all five cards share a suit iff the first card's suit accounts for all of them
	bool flush = (suitCounts[cards[0].suit] == HAND_SIZE);

	// Five distinct ranks spanning exactly five values is a straight. The wheel
	// (A-2-3-4-5) needs its own test because the ace is stored as the high rank.
	bool straight = (distinct == HAND_SIZE && highest - lowest == HAND_SIZE - 1);
	bool wheel = (distinct == HAND_SIZE && rankCounts[0] && rankCounts[1] &&
				  rankCounts[2] && rankCounts[3] && rankCounts[12]);

	if (flush && straight && lowest == 8) return HAND_ROYAL_FLUSH;	// T J Q K A
	if (flush && (straight || wheel)) return HAND_STRAIGHT_FLUSH;
	if (quads) return HAND_FOUR_OF_A_KIND;
	if (trips && pairs) return HAND_FULL_HOUSE;
	if (flush) return HAND_FLUSH;
	if (straight || wheel) return HAND_STRAIGHT;
	if (trips) return HAND_THREE_OF_A_KIND;
	if (pairs == 2) return HAND_TWO_PAIR;
	if (pairs == 1 && highPair) return HAND_JACKS_OR_BETTER;

	return HAND_NOTHING;
}

static void DealNewHand(void)
{
	ShuffleDeck();
	for (int i = 0; i < HAND_SIZE; i++)
	{
		hand[i] = DrawCard();
		held[i] = false;
	}

	credits -= bet;
	lastWin = 0;
	lastRank = HAND_NOTHING;
	state = STATE_DRAW;

	PlaySound(sounds[SND_DEAL]);
}

static void DrawReplacements(void)
{
	for (int i = 0; i < HAND_SIZE; i++)
	{
		if (!held[i]) hand[i] = DrawCard();

		// clear the holds so the result screen doesn't keep showing gold
		// "HELD" badges on cards that are no longer selectable
		held[i] = false;
	}

	lastRank = EvaluateHand(hand);
	lastWin = payouts[lastRank] * bet;
	credits += lastWin;
	state = STATE_RESULT;

	PlaySound(sounds[lastWin > 0 ? SND_WIN : SND_LOSE]);
}

static void ResetGame(void)
{
	credits = STARTING_CREDITS;
	bet = MAX_BET;
	lastWin = 0;
	lastRank = HAND_NOTHING;
	state = STATE_BET;

	for (int i = 0; i < HAND_SIZE; i++) held[i] = false;

	ShuffleDeck();
}

// ---------------------------------------------------------------- input

static Rectangle CardSlotRect(int index)
{
	int totalWidth = HAND_SIZE * CARD_WIDTH + (HAND_SIZE - 1) * CARD_SPACING;
	int startX = (SCREEN_WIDTH - totalWidth) / 2;

	return (Rectangle){ (float)(startX + index * (CARD_WIDTH + CARD_SPACING)),
						(float)CARD_TOP, (float)CARD_WIDTH, (float)CARD_HEIGHT };
}

static Rectangle VolumeBarRect(void)
{
	return (Rectangle){ SCREEN_WIDTH - 190.0f, 44.0f, 150.0f, 18.0f };
}

// Runs in every state, ahead of the state machine, so audio can always be
// adjusted. The widget sits well clear of the card rects, so a click here can
// never also register as a hold toggle.
static void HandleVolumeInput(void)
{
	int previous = volumeLevel;
	bool wasMuted = muted;

	if (IsKeyPressed(KEY_M))
	{
		muted = !muted;
		// unmuting at zero would be silent anyway, so give it something to play
		if (!muted && volumeLevel == 0) volumeLevel = 1;
	}

	if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) volumeLevel--;
	if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) volumeLevel++;

	// Click or drag along the bar to set the level directly. The drag is latched
	// on press so it survives the cursor wandering off the 18px-tall bar, and so
	// a drag that began elsewhere on screen can't hijack the slider.
	Rectangle bar = VolumeBarRect();
	Vector2 mouse = GetMousePosition();

	if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, bar))
		draggingVolume = true;
	else if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
		draggingVolume = false;

	if (draggingVolume)
		volumeLevel = (int)(((mouse.x - bar.x) / bar.width)*VOLUME_STEPS + 0.5f);

	if (volumeLevel < 0) volumeLevel = 0;
	if (volumeLevel > VOLUME_STEPS) volumeLevel = VOLUME_STEPS;

	if (volumeLevel != previous) muted = false;		// reaching for the level unmutes

	if (volumeLevel != previous || muted != wasMuted)
	{
		SetMasterVolume(muted ? 0.0f : (float)volumeLevel/VOLUME_STEPS);
		if (!muted) PlaySound(sounds[SND_HOLD]);		// preview the new level
	}
}

static void HandleInput(void)
{
	HandleVolumeInput();

	if (state == STATE_DRAW)
	{
		// toggle holds with 1-5 or by clicking a card
		bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

		for (int i = 0; i < HAND_SIZE; i++)
		{
			if (IsKeyPressed(KEY_ONE + i) ||
				(clicked && CheckCollisionPointRec(GetMousePosition(), CardSlotRect(i))))
			{
				held[i] = !held[i];
				PlaySound(sounds[SND_HOLD]);
			}
		}

		if (IsKeyPressed(KEY_SPACE)) DrawReplacements();
	}
	else	// STATE_BET or STATE_RESULT
	{
		if (IsKeyPressed(KEY_UP) && bet < MAX_BET) bet++;
		if (IsKeyPressed(KEY_DOWN) && bet > 1) bet--;
		if (IsKeyPressed(KEY_R)) ResetGame();
		if (IsKeyPressed(KEY_SPACE) && credits >= bet) DealNewHand();
	}
}

// ---------------------------------------------------------------- drawing

static void DrawCenteredText(const char *text, int y, int size, Color color)
{
	DrawText(text, SCREEN_WIDTH / 2 - MeasureText(text, size) / 2, y, size, color);
}

// raylib culls triangles whose vertices aren't counter-clockwise. These wrap
// the winding used by raylib's own shapes example so callers can't get it wrong.
static void DrawTriUp(Vector2 apex, Vector2 baseLeft, Vector2 baseRight, Color color)
{
	DrawTriangle(apex, baseLeft, baseRight, color);
}

static void DrawTriDown(Vector2 apex, Vector2 baseLeft, Vector2 baseRight, Color color)
{
	DrawTriangle(apex, baseRight, baseLeft, color);
}

// Pips are built from circles and triangles because raylib's default font is
// ASCII-only and has no suit glyphs. `r` is roughly the pip's half-height.
static void DrawSuit(Suit suit, Vector2 c, float r, Color color)
{
	switch (suit)
	{
	case SUIT_SPADES:
		DrawTriUp((Vector2){ c.x, c.y - 1.00f*r },
				  (Vector2){ c.x - 0.88f*r, c.y + 0.25f*r },
				  (Vector2){ c.x + 0.88f*r, c.y + 0.25f*r }, color);
		DrawCircleV((Vector2){ c.x - 0.42f*r, c.y + 0.25f*r }, 0.46f*r, color);
		DrawCircleV((Vector2){ c.x + 0.42f*r, c.y + 0.25f*r }, 0.46f*r, color);
		DrawTriUp((Vector2){ c.x, c.y + 0.25f*r },			// stem
				  (Vector2){ c.x - 0.32f*r, c.y + 1.00f*r },
				  (Vector2){ c.x + 0.32f*r, c.y + 1.00f*r }, color);
		break;

	case SUIT_HEARTS:
		DrawCircleV((Vector2){ c.x - 0.42f*r, c.y - 0.30f*r }, 0.46f*r, color);
		DrawCircleV((Vector2){ c.x + 0.42f*r, c.y - 0.30f*r }, 0.46f*r, color);
		DrawTriDown((Vector2){ c.x, c.y + 1.00f*r },
					(Vector2){ c.x - 0.88f*r, c.y - 0.30f*r },
					(Vector2){ c.x + 0.88f*r, c.y - 0.30f*r }, color);
		break;

	case SUIT_DIAMONDS:
		DrawTriUp((Vector2){ c.x, c.y - 1.00f*r },
				  (Vector2){ c.x - 0.68f*r, c.y },
				  (Vector2){ c.x + 0.68f*r, c.y }, color);
		DrawTriDown((Vector2){ c.x, c.y + 1.00f*r },
					(Vector2){ c.x - 0.68f*r, c.y },
					(Vector2){ c.x + 0.68f*r, c.y }, color);
		break;

	case SUIT_CLUBS:
		DrawCircleV((Vector2){ c.x, c.y - 0.45f*r }, 0.44f*r, color);
		DrawCircleV((Vector2){ c.x - 0.50f*r, c.y + 0.30f*r }, 0.44f*r, color);
		DrawCircleV((Vector2){ c.x + 0.50f*r, c.y + 0.30f*r }, 0.44f*r, color);
		DrawTriUp((Vector2){ c.x, c.y + 0.10f*r },			// stem
				  (Vector2){ c.x - 0.30f*r, c.y + 1.00f*r },
				  (Vector2){ c.x + 0.30f*r, c.y + 1.00f*r }, color);
		break;
	}
}

static void DrawVolumeControl(void)
{
	Rectangle bar = VolumeBarRect();
	Vector2 icon = { bar.x - 34.0f, bar.y + bar.height*0.5f };
	Color tint = muted ? (Color){ 118, 132, 124, 255 } : RAYWHITE;

	// speaker: magnet block plus a cone flaring right (vertices wound to match
	// raylib's counter-clockwise requirement, as in DrawTriUp/DrawTriDown)
	DrawRectangleRec((Rectangle){ icon.x - 9, icon.y - 4, 7, 8 }, tint);
	DrawTriangle((Vector2){ icon.x - 9, icon.y },
				 (Vector2){ icon.x + 2, icon.y + 9 },
				 (Vector2){ icon.x + 2, icon.y - 9 }, tint);

	if (muted)
	{
		DrawLineEx((Vector2){ icon.x + 8, icon.y - 6 }, (Vector2){ icon.x + 18, icon.y + 6 }, 2.0f, tint);
		DrawLineEx((Vector2){ icon.x + 18, icon.y - 6 }, (Vector2){ icon.x + 8, icon.y + 6 }, 2.0f, tint);
	}
	else
	{
		DrawRing(icon, 11.0f, 13.0f, -45.0f, 45.0f, 16, tint);
		DrawRing(icon, 16.0f, 18.0f, -45.0f, 45.0f, 16, tint);
	}

	int filled = muted ? 0 : volumeLevel;
	float segWidth = bar.width / VOLUME_STEPS;
	for (int i = 0; i < VOLUME_STEPS; i++)
	{
		Rectangle seg = { bar.x + i*segWidth + 1.5f, bar.y, segWidth - 3.0f, bar.height };
		DrawRectangleRounded(seg, 0.35f, 4, (i < filled) ? GOLD : (Color){ 255, 255, 255, 40 });
	}

	const char *hint = muted ? "M to unmute" : "-/+ volume   M mute";
	DrawText(hint, (int)(bar.x + bar.width) - MeasureText(hint, 14), (int)bar.y + 26, 14,
			 (Color){ 150, 170, 158, 255 });
}

static void DrawCardShadow(Rectangle slot)
{
	DrawRectangleRounded((Rectangle){ slot.x + 4, slot.y + 5, slot.width, slot.height },
						 CARD_ROUNDNESS, 8, CARD_SHADOW);
}

static void DrawCardFace(Card card, Rectangle slot, bool isHeld)
{
	const char *rank = rankNames[card.rank];
	Color ink = (card.suit == SUIT_HEARTS || card.suit == SUIT_DIAMONDS) ? CARD_RED : CARD_BLACK;
	int rankWidth = MeasureText(rank, CORNER_FONT);

	DrawCardShadow(slot);
	DrawRectangleRounded(slot, CARD_ROUNDNESS, 8, CARD_FACE);
	DrawRectangleRoundedLinesEx(slot, CARD_ROUNDNESS, 8, 2.0f, CARD_EDGE);

	// corner index, repeated in the opposite corner (upright, not rotated)
	DrawText(rank, (int)slot.x + 12, (int)slot.y + 9, CORNER_FONT, ink);
	DrawSuit(card.suit, (Vector2){ slot.x + 12 + rankWidth*0.5f, slot.y + 54 }, 9.0f, ink);

	DrawText(rank, (int)(slot.x + slot.width) - 12 - rankWidth,
			 (int)(slot.y + slot.height) - 9 - CORNER_FONT, CORNER_FONT, ink);
	DrawSuit(card.suit, (Vector2){ slot.x + slot.width - 12 - rankWidth*0.5f,
								   slot.y + slot.height - 54 }, 9.0f, ink);

	DrawSuit(card.suit, (Vector2){ slot.x + slot.width*0.5f, slot.y + slot.height*0.5f },
			 35.0f, ink);

	if (isHeld)
	{
		DrawRectangleRoundedLinesEx(slot, CARD_ROUNDNESS, 8, 4.0f, GOLD);

		Rectangle badge = { slot.x + slot.width*0.5f - 38, slot.y + slot.height + 10, 76, 28 };
		DrawRectangleRounded(badge, 0.5f, 8, GOLD);
		DrawText("HELD", (int)(badge.x + (badge.width - MeasureText("HELD", 18))*0.5f),
				 (int)badge.y + 5, 18, (Color){ 45, 33, 0, 255 });
	}
}

static void DrawCardBack(Rectangle slot)
{
	DrawCardShadow(slot);
	DrawRectangleRounded(slot, CARD_ROUNDNESS, 8, CARD_BACK);
	DrawRectangleRoundedLinesEx(slot, CARD_ROUNDNESS, 8, 2.0f, CARD_BACK_EDGE);

	// diagonal lattice, scissored to an inset panel so it can't spill past the edge
	Rectangle inner = { slot.x + 10, slot.y + 10, slot.width - 20, slot.height - 20 };
	BeginScissorMode((int)inner.x, (int)inner.y, (int)inner.width, (int)inner.height);
	for (float d = -inner.height; d < inner.width; d += 14.0f)
	{
		DrawLineEx((Vector2){ inner.x + d, inner.y },
				   (Vector2){ inner.x + d + inner.height, inner.y + inner.height },
				   2.0f, CARD_BACK_LINE);
	}
	EndScissorMode();

	DrawRectangleRoundedLinesEx(inner, 0.08f, 8, 1.0f, CARD_BACK_EDGE);
}

static void DrawPayTable(void)
{
	DrawText("PAYTABLE (x bet)", SCREEN_MARGIN, PAYTABLE_Y - 30, 22, GRAY);

	// 9 rows at this pitch end at y=278, clearing CARD_TOP
	int y = PAYTABLE_Y;
	for (int i = HAND_COUNT - 1; i >= HAND_JACKS_OR_BETTER; i--)
	{
		Color color = ((int)lastRank == i) ? GOLD : LIGHTGRAY;
		DrawText(handNames[i], SCREEN_MARGIN, y, 20, color);
		DrawText(TextFormat("%d", payouts[i]), SCREEN_MARGIN + 220, y, 20, color);
		y += PAYTABLE_PITCH;
	}
}

static void DrawScene(void)
{
	ClearBackground(TABLE_FELT);

	DrawCenteredText("VIDEO POKER", TITLE_Y, 44, RAYWHITE);
	DrawPayTable();
	DrawVolumeControl();

	for (int i = 0; i < HAND_SIZE; i++)
	{
		Rectangle slot = CardSlotRect(i);

		// face-down until the first deal
		if (state == STATE_BET) DrawCardBack(slot);
		else DrawCardFace(hand[i], slot, held[i]);
	}

	const char *message;
	if (state == STATE_BET) message = "SPACE to deal - UP/DOWN to change bet";
	else if (state == STATE_DRAW) message = "Click cards or press 1-5 to hold - SPACE to draw";
	else if (lastWin > 0) message = TextFormat("%s - you win %d!", handNames[lastRank], lastWin);
	else message = "No win - SPACE to deal again";

	DrawCenteredText(message, STATUS_Y, 24, RAYWHITE);

	DrawText(TextFormat("CREDITS: %d", credits), SCREEN_MARGIN, FOOTER_Y, 28, GOLD);

	const char *betText = TextFormat("BET: %d", bet);
	DrawText(betText, SCREEN_WIDTH - SCREEN_MARGIN - MeasureText(betText, 28), FOOTER_Y, 28, GOLD);

	if (credits < bet && state != STATE_DRAW)
	{
		DrawCenteredText("Not enough credits - lower your bet or press R to restart",
						 WARNING_Y, 20, RED);
	}
}

int main(void)
{
	// No FLAG_WINDOW_HIGHDPI: the whole layout is in hardcoded pixels, and on a
	// scaled display the flag desynchronises the framebuffer from the coordinates
	// used for drawing and mouse hit-testing.
	SetConfigFlags(FLAG_VSYNC_HINT);
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Video Poker");
	InitAudioDevice();
	SetTargetFPS(60);

	SetRandomSeed((unsigned int)time(NULL));
	LoadSounds();
	SetMasterVolume((float)volumeLevel/VOLUME_STEPS);
	ShuffleDeck();

	while (!WindowShouldClose())
	{
		HandleInput();

		BeginDrawing();
		DrawScene();
		EndDrawing();
	}

	UnloadSounds();
	CloseAudioDevice();
	CloseWindow();
	return 0;
}
