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
