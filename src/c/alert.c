#include "alert.h"

// ---------------------------------------------------------------------------
// Vibration: built-in patterns. Alternating on/off in ms, starting with "on".
// One cycle ends with a pause; while ringing it repeats.
// ---------------------------------------------------------------------------

static const uint32_t PAT_STANDARD[]  = {800, 400};
static const uint32_t PAT_PULSE[]     = {200, 200, 200, 200, 200, 600};
static const uint32_t PAT_DOUBLE[]    = {350, 150, 350, 800};
static const uint32_t PAT_HEARTBEAT[] = {120, 100, 280, 900};
static const uint32_t PAT_RISING[]    = {100, 400, 200, 300, 400, 200, 800, 700};
static const uint32_t PAT_SOS[]       = {150, 100, 150, 100, 150, 300,
                                         450, 100, 450, 100, 450, 300,
                                         150, 100, 150, 100, 150, 1000};
static const uint32_t PAT_LONG[]      = {1500, 300};
static const uint32_t PAT_GENTLE[]    = {80, 920};
// Aurora: pulses get longer and denser (rising), hold briefly at the top,
// then shorter and looser again (fading), followed by a rest - like a flickering aurora
static const uint32_t PAT_AURORA[]    = {60, 160, 100, 120, 160, 90, 240, 60,   // rising
                                         700, 60,                                // hold
                                         240, 90, 160, 120, 100, 160, 60, 1400}; // fading

typedef struct {
  const uint32_t *durations;
  uint32_t count;
} PatternDef;

#define PAT(x) {x, ARRAY_LENGTH(x)}
static const PatternDef PATTERNS[VIBE_PATTERN_COUNT] = {
  PAT(PAT_STANDARD), PAT(PAT_PULSE), PAT(PAT_DOUBLE), PAT(PAT_HEARTBEAT),
  PAT(PAT_RISING), PAT(PAT_SOS), PAT(PAT_LONG), PAT(PAT_GENTLE), PAT(PAT_AURORA),
};

static VibePattern_t s_vibe;
static uint32_t s_cycle_ms;
static AppTimer *s_vibe_timer;

static const PatternDef *prv_pattern(VibePattern_t p) {
  return &PATTERNS[p < VIBE_PATTERN_COUNT ? p : DEFAULT_VIBE_PATTERN];
}

static void prv_enqueue(VibePattern_t p) {
  const PatternDef *def = prv_pattern(p);
  // Do not send the last pause (the pattern should end with vibration); the timer covers it
  uint32_t count = (def->count % 2 == 0) ? def->count - 1 : def->count;
  VibePattern pat = {.durations = def->durations, .num_segments = count};
  vibes_enqueue_custom_pattern(pat);
}

static uint32_t prv_cycle_ms(VibePattern_t p) {
  const PatternDef *def = prv_pattern(p);
  uint32_t total = 0;
  for (uint32_t i = 0; i < def->count; i++) total += def->durations[i];
  return total;
}

static void prv_vibe_cycle(void *context) {
  prv_enqueue(s_vibe);
  s_vibe_timer = app_timer_register(s_cycle_ms, prv_vibe_cycle, NULL);
}

void alert_preview_vibe(VibePattern_t pattern) {
  // Looping preview (vibration only) until alert_stop()
  AlertParams p = {.vibe_pattern = pattern, .vibrate = true, .sound = ALARM_SOUND_OFF};
  alert_start(&p);
}

// ---------------------------------------------------------------------------
// Sound (own melodies; the built-in watch sounds are not available to apps)
// ---------------------------------------------------------------------------

#define NOTE(midi, ms, wave) {.midi_note = (midi), .waveform = (wave), .duration_ms = (ms), .velocity = 0}
#define REST(ms) NOTE(0, ms, SpeakerWaveformSine)
#define TRI SpeakerWaveformTriangle
#define SQR SpeakerWaveformSquare
#define SIN SpeakerWaveformSine

// Reveille: the traditional US bugle call (public domain), 2/4 at quarter = 152,
// sounding pitch of a B-flat bugle (written G4 C5 E5 G5 -> F4 Bb4 D5 F5).
// RV_N = detached note (short gap after it, so repeated notes stay distinct),
// RV_L = slurred note (no gap). Lengths in sixteenths.
#define RV_16 99
#define RV_GAP 25
#define RV_N(midi, n16) NOTE(midi, (n16) * RV_16 - RV_GAP, TRI), REST(RV_GAP)
#define RV_L(midi, n16) NOTE(midi, (n16) * RV_16, TRI)
#define RV_G 65
#define RV_C 70
#define RV_E 74
#define RV_GH 77
// "c e/c g e" bar and its two variants
#define RV_BAR_A RV_N(RV_C, 2), RV_L(RV_E, 1), RV_N(RV_C, 1), RV_N(RV_G, 2), RV_N(RV_E, 2)
#define RV_BAR_B RV_N(RV_C, 2), RV_L(RV_E, 1), RV_N(RV_C, 1), RV_N(RV_G, 2), RV_N(RV_C, 2)
#define RV_BAR_C RV_N(RV_C, 2), RV_L(RV_E, 1), RV_N(RV_C, 1), RV_N(RV_G, 2), RV_N(RV_G, 2)
#define RV_BAR_D RV_L(RV_E, 4), RV_N(RV_C, 2), RV_N(RV_G, 2)
#define RV_FIRST_PART RV_BAR_A, RV_BAR_A, RV_BAR_B, RV_BAR_D, RV_BAR_A, RV_BAR_A, RV_BAR_C

static const SpeakerNote MELODY_REVEILLE[] = {
  RV_N(RV_G, 2),                                                          // pickup
  RV_FIRST_PART,                                                          // bars 1-7
  RV_N(RV_C, 6), RV_N(RV_E, 2),                                           // bar 8
  RV_N(RV_E, 2), RV_N(RV_E, 2), RV_N(RV_E, 2), RV_N(RV_E, 2),             // bar 9
  RV_L(RV_GH, 4), RV_N(RV_E, 2), RV_N(RV_C, 2),                           // bar 10
  RV_N(RV_E, 2), RV_N(RV_C, 2), RV_N(RV_E, 2), RV_N(RV_C, 2),             // bar 11
  RV_L(RV_E, 4), RV_N(RV_C, 2), RV_N(RV_E, 2),                            // bar 12
  RV_N(RV_E, 2), RV_N(RV_E, 2), RV_N(RV_E, 2), RV_N(RV_E, 2),             // bar 13
  RV_L(RV_GH, 4), RV_N(RV_E, 2), RV_N(RV_C, 2),                           // bar 14
  RV_N(RV_E, 2), RV_N(RV_C, 2), RV_N(RV_G, 2), RV_N(RV_G, 2),             // bar 15
  RV_N(RV_C, 6), RV_N(RV_G, 2),                                           // bar 16
  RV_FIRST_PART,                                                          // bars 17-23
  RV_N(RV_C, 6),                                                          // bar 24
  REST(1200),
};

static const SpeakerNote MELODY_BEEP[] = {
  NOTE(81, 150, SQR), REST(100), NOTE(81, 150, SQR), REST(100), NOTE(81, 150, SQR), REST(700),
};

static const SpeakerNote MELODY_CHIME[] = {
  NOTE(88, 500, SIN), NOTE(84, 900, SIN), REST(1200),
};

// Rising: like a beep, gets louder with every repetition
static const SpeakerNote MELODY_RISING[] = {
  NOTE(84, 200, SQR), REST(150), NOTE(84, 200, SQR), REST(700),
};

// Korobeiniki: Russian folk song (public domain), the melody known from Tetris.
// A minor, quarter = 150; lengths in eighths, short gap after every note.
#define KB_8 200
#define KB_GAP 30
#define KB_N(midi, n8) NOTE(midi, (n8) * KB_8 - KB_GAP, SQR), REST(KB_GAP)
#define KB_A KB_N(76, 2), KB_N(71, 1), KB_N(72, 1), KB_N(74, 2), KB_N(72, 1), KB_N(71, 1), \
             KB_N(69, 2), KB_N(69, 1), KB_N(72, 1), KB_N(76, 2), KB_N(74, 1), KB_N(72, 1), \
             KB_N(71, 3), KB_N(72, 1), KB_N(74, 2), KB_N(76, 2), \
             KB_N(72, 2), KB_N(69, 2), KB_N(69, 2), REST(2 * KB_8)
#define KB_B REST(KB_8), KB_N(74, 2), KB_N(77, 1), KB_N(81, 2), KB_N(79, 1), KB_N(77, 1), \
             KB_N(76, 3), KB_N(72, 1), KB_N(76, 2), KB_N(74, 1), KB_N(72, 1), \
             KB_N(71, 2), KB_N(71, 1), KB_N(72, 1), KB_N(74, 2), KB_N(76, 2), \
             KB_N(72, 2), KB_N(69, 2), KB_N(69, 2), REST(2 * KB_8)

static const SpeakerNote MELODY_KOROBEINIKI[] = {
  KB_A, KB_A, KB_B, KB_B, REST(4 * KB_8),
};

// Westminster Quarters (public domain): the full-hour chime, changes 2-5, E major,
// one octave above the real bells so the small speaker can play it.
#define WQ_Q 600
#define WQ_N(midi, n) NOTE(midi, (n) * WQ_Q - 30, SIN), REST(30)
static const SpeakerNote MELODY_WESTMINSTER[] = {
  WQ_N(76, 1), WQ_N(80, 1), WQ_N(78, 1), WQ_N(71, 2),
  WQ_N(76, 1), WQ_N(78, 1), WQ_N(80, 1), WQ_N(76, 2),
  WQ_N(80, 1), WQ_N(76, 1), WQ_N(78, 1), WQ_N(71, 2),
  WQ_N(71, 1), WQ_N(78, 1), WQ_N(80, 1), WQ_N(76, 2),
  REST(2 * WQ_Q),
};

// Yurop: "Ode to Joy" from Beethoven's 9th symphony (public domain), the melody of the
// European anthem (Beethoven's original tune, not the copyrighted orchestral arrangement).
// D major, quarter = 120; lengths in eighths.
#define YU_8 250
#define YU_N(midi, n8) NOTE(midi, (n8) * YU_8 - 30, TRI), REST(30)
#define YU_D 74
#define YU_E 76
#define YU_FS 78
#define YU_G 79
#define YU_A 81
#define YU_LA 69
#define YU_LINE YU_N(YU_FS, 2), YU_N(YU_FS, 2), YU_N(YU_G, 2), YU_N(YU_A, 2), \
                YU_N(YU_A, 2), YU_N(YU_G, 2), YU_N(YU_FS, 2), YU_N(YU_E, 2), \
                YU_N(YU_D, 2), YU_N(YU_D, 2), YU_N(YU_E, 2), YU_N(YU_FS, 2)
static const SpeakerNote MELODY_YUROP[] = {
  YU_LINE, YU_N(YU_FS, 3), YU_N(YU_E, 1), YU_N(YU_E, 4),
  YU_LINE, YU_N(YU_E, 3), YU_N(YU_D, 1), YU_N(YU_D, 4),
  YU_N(YU_E, 2), YU_N(YU_E, 2), YU_N(YU_FS, 2), YU_N(YU_D, 2),
  YU_N(YU_E, 2), YU_N(YU_FS, 1), YU_N(YU_G, 1), YU_N(YU_FS, 2), YU_N(YU_D, 2),
  YU_N(YU_E, 2), YU_N(YU_FS, 1), YU_N(YU_G, 1), YU_N(YU_FS, 2), YU_N(YU_E, 2),
  YU_N(YU_D, 2), YU_N(YU_E, 2), YU_N(YU_LA, 4),
  YU_LINE, YU_N(YU_E, 3), YU_N(YU_D, 1), YU_N(YU_D, 4),
  REST(4 * YU_8),
};

static AlarmSound s_sound;
static uint8_t s_volume;
static bool s_sound_on;

static void prv_play_once(void);

static void prv_sound_finished(SpeakerFinishReason reason, void *ctx) {
  if (!s_sound_on || reason != SpeakerFinishReasonDone) return;
  if (s_sound == ALARM_SOUND_RISING && s_volume < 100) {
    s_volume = s_volume + 10 > 100 ? 100 : s_volume + 10;
  }
  prv_play_once();
}

static void prv_play_once(void) {
  const SpeakerNote *notes = NULL;
  uint32_t count = 0;
  switch (s_sound) {
    case ALARM_SOUND_REVEILLE: notes = MELODY_REVEILLE; count = ARRAY_LENGTH(MELODY_REVEILLE); break;
    case ALARM_SOUND_BEEP:     notes = MELODY_BEEP;     count = ARRAY_LENGTH(MELODY_BEEP); break;
    case ALARM_SOUND_CHIME:    notes = MELODY_CHIME;    count = ARRAY_LENGTH(MELODY_CHIME); break;
    case ALARM_SOUND_RISING:   notes = MELODY_RISING;   count = ARRAY_LENGTH(MELODY_RISING); break;
    case ALARM_SOUND_KOROBEINIKI:
      notes = MELODY_KOROBEINIKI; count = ARRAY_LENGTH(MELODY_KOROBEINIKI); break;
    case ALARM_SOUND_WESTMINSTER:
      notes = MELODY_WESTMINSTER; count = ARRAY_LENGTH(MELODY_WESTMINSTER); break;
    case ALARM_SOUND_YUROP:
      notes = MELODY_YUROP; count = ARRAY_LENGTH(MELODY_YUROP); break;
    default: return;
  }
  if (!speaker_play_notes(notes, count, s_volume)) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Speaker not available");
    s_sound_on = false;
  }
}

// ---------------------------------------------------------------------------

void alert_start(const AlertParams *p) {
  alert_stop();
  if (p->vibrate) {
    s_vibe = p->vibe_pattern;
    s_cycle_ms = prv_cycle_ms(s_vibe);
    prv_vibe_cycle(NULL);
  }
  AlarmSound sound = p->sound;
  if (sound != ALARM_SOUND_OFF) {
    if (speaker_is_muted()) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Speaker muted, vibration only");
    } else {
      s_sound = sound;
      s_volume = (sound == ALARM_SOUND_RISING) ? 30 : 80;
      s_sound_on = true;
      speaker_set_finish_callback(prv_sound_finished, NULL);
      prv_play_once();
    }
  }
}

void alert_stop(void) {
  if (s_vibe_timer) {
    app_timer_cancel(s_vibe_timer);
    s_vibe_timer = NULL;
  }
  vibes_cancel();
  if (s_sound_on) {
    s_sound_on = false;
    speaker_set_finish_callback(NULL, NULL);
    speaker_stop();
  }
}
