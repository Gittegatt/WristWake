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

// Rising Beep: like a beep, gets louder with every repetition
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

// Field Agent: own electronic pattern - two flickering double tones, a short pause,
// then a quick rising confirmation tone.
#define FA_FLICK NOTE(88, 30, SQR), NOTE(93, 30, SQR), NOTE(88, 30, SQR), NOTE(93, 30, SQR), \
                 NOTE(88, 30, SQR), NOTE(93, 30, SQR), NOTE(88, 30, SQR), NOTE(93, 30, SQR)
static const SpeakerNote MELODY_FIELD_AGENT[] = {
  FA_FLICK, REST(90), FA_FLICK, REST(260),
  NOTE(81, 70, SQR), REST(20), NOTE(88, 70, SQR), REST(20), NOTE(93, 160, SQR),
  REST(900),
};

// Maple Leaf Rag (Scott Joplin, 1899, public domain): the famous opening four bars,
// top voice of the first edition, A-flat major, 2/4; lengths in sixteenths.
#define ML_16 160
#define ML(midi, n16) NOTE(midi, (n16) * ML_16 - 25, TRI), REST(25)
static const SpeakerNote MELODY_MAPLE_LEAF[] = {
  REST(ML_16), ML(68, 1), ML(75, 1), ML(68, 1), ML(72, 1), ML(75, 2), ML(67, 1),   // bar 1
  ML(75, 1), ML(67, 1), ML(70, 1), ML(75, 3), REST(2 * ML_16),                     // bar 2
  REST(ML_16), ML(68, 1), ML(75, 1), ML(68, 1), ML(72, 1), ML(75, 2), ML(67, 1),   // bar 3
  ML(63, 1), ML(67, 1), ML(70, 1), ML(75, 2), REST(ML_16), ML(75, 1),              // bar 4
  REST(4 * ML_16),
};

// Rondo alla Turca (Mozart, piano sonata K. 331, public domain): opening theme, A minor,
// 2/4 at quarter = 120; lengths in sixteenths.
#define RO_16 125
#define RO(midi, n16) NOTE(midi, (n16) * RO_16 - 20, TRI), REST(20)
static const SpeakerNote MELODY_RONDO[] = {
  RO(71, 1), RO(69, 1), RO(68, 1), RO(69, 1),                                       // pickup
  RO(72, 2), REST(2 * RO_16), RO(74, 1), RO(72, 1), RO(71, 1), RO(72, 1),
  RO(76, 2), REST(2 * RO_16), RO(77, 1), RO(76, 1), RO(75, 1), RO(76, 1),
  RO(83, 1), RO(81, 1), RO(80, 1), RO(81, 1), RO(83, 1), RO(81, 1), RO(80, 1), RO(81, 1),
  RO(84, 4), RO(81, 2), RO(84, 2),
  RO(83, 2), RO(81, 2), RO(79, 2), RO(81, 2),
  RO(83, 2), RO(81, 2), RO(79, 2), RO(81, 2),
  RO(83, 2), RO(81, 2), RO(79, 2), RO(78, 2),
  RO(76, 4), REST(4 * RO_16),
};

// Generic patterns (own designs)
// Classic Ring: old telephone bell - fast trill of two tones, "ring-ring ... pause"
#define CR_TRILL NOTE(88, 25, SQR), NOTE(91, 25, SQR)
#define CR_TRILL4 CR_TRILL, CR_TRILL, CR_TRILL, CR_TRILL
#define CR_RING CR_TRILL4, CR_TRILL4, CR_TRILL4, CR_TRILL4
static const SpeakerNote MELODY_CLASSIC_RING[] = {
  CR_RING, REST(300), CR_RING, REST(1800),
};

// Digital: classic digital alarm clock - four quick beeps, pause
static const SpeakerNote MELODY_DIGITAL[] = {
  NOTE(95, 70, SQR), REST(60), NOTE(95, 70, SQR), REST(60),
  NOTE(95, 70, SQR), REST(60), NOTE(95, 70, SQR), REST(60), REST(500),
};

// Rising Sunrise: gentle rising major arpeggio, gets louder with every repetition
static const SpeakerNote MELODY_SUNRISE[] = {
  NOTE(72, 220, SIN), NOTE(76, 220, SIN), NOTE(79, 220, SIN), NOTE(84, 220, SIN),
  NOTE(88, 600, SIN), REST(900),
};

// Gentle melodies like Rising Sunrise (own designs), louder with every repetition
// Rising Daybreak: pentatonic run upwards, held top note, soft echo back down
static const SpeakerNote MELODY_DAYBREAK[] = {
  NOTE(74, 180, SIN), NOTE(76, 180, SIN), NOTE(79, 180, SIN), NOTE(81, 180, SIN), NOTE(83, 180, SIN),
  NOTE(86, 700, SIN), REST(120), NOTE(83, 300, SIN), NOTE(81, 500, SIN), REST(1000),
};

// Rising Breeze: wave up and down twice, like a soft gust of wind
static const SpeakerNote MELODY_BREEZE[] = {
  NOTE(69, 200, TRI), NOTE(72, 200, TRI), NOTE(76, 200, TRI), NOTE(79, 300, TRI),
  NOTE(76, 200, TRI), NOTE(72, 200, TRI),
  NOTE(67, 200, TRI), NOTE(71, 200, TRI), NOTE(74, 200, TRI), NOTE(78, 300, TRI),
  NOTE(74, 200, TRI), NOTE(71, 200, TRI),
  NOTE(69, 700, TRI), REST(1000),
};

// Rising Bloom: small waltz-like phrase in 3/4 that opens up and closes again
static const SpeakerNote MELODY_BLOOM[] = {
  NOTE(65, 300, SIN), NOTE(69, 300, SIN), NOTE(72, 300, SIN), NOTE(77, 600, SIN), NOTE(76, 300, SIN),
  NOTE(74, 300, SIN), NOTE(72, 300, SIN), NOTE(69, 300, SIN), NOTE(72, 900, SIN), REST(900),
};

// Siren: two-tone "hi-lo"
static const SpeakerNote MELODY_SIREN[] = {
  NOTE(81, 450, TRI), NOTE(76, 450, TRI), NOTE(81, 450, TRI), NOTE(76, 450, TRI), REST(400),
};

// Chirp: quick rising chirps like a bird
#define CH_CHIRP NOTE(88, 40, SIN), NOTE(91, 40, SIN), NOTE(95, 40, SIN), REST(120)
static const SpeakerNote MELODY_CHIRP[] = {
  CH_CHIRP, CH_CHIRP, CH_CHIRP, REST(900),
};

// Pulse: soft single tones that come faster and faster
#define PU(gap) NOTE(84, 120, TRI), REST(gap)
static const SpeakerNote MELODY_PULSE[] = {
  PU(700), PU(600), PU(500), PU(400), PU(300), PU(220), PU(160), PU(120), PU(120), PU(120), REST(800),
};

static AlarmSound s_sound;
// Speaker volume in percent (the watch speaker is loud; 80 % and 40 % were too loud on the real watch)
#define VOLUME_NORMAL 25
#define VOLUME_RISE_START 10   // the "Rising ..." sounds start quiet ...
#define VOLUME_RISE_STEP 3     // ... and get louder with every repetition ...
#define VOLUME_RISE_MAX 35     // ... up to this level

static uint8_t s_volume;

// The "Rising ..." sounds get louder with every repetition
static bool prv_is_rising(AlarmSound sound) {
  return sound == ALARM_SOUND_RISING || sound == ALARM_SOUND_SUNRISE ||
         sound == ALARM_SOUND_DAYBREAK || sound == ALARM_SOUND_BREEZE || sound == ALARM_SOUND_BLOOM;
}
static bool s_sound_on;

static void prv_play_once(void);

static void prv_sound_finished(SpeakerFinishReason reason, void *ctx) {
  if (!s_sound_on || reason != SpeakerFinishReasonDone) return;
  if (prv_is_rising(s_sound) && s_volume < VOLUME_RISE_MAX) {
    s_volume = s_volume + VOLUME_RISE_STEP > VOLUME_RISE_MAX ? VOLUME_RISE_MAX : s_volume + VOLUME_RISE_STEP;
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
    case ALARM_SOUND_FIELD_AGENT:
      notes = MELODY_FIELD_AGENT; count = ARRAY_LENGTH(MELODY_FIELD_AGENT); break;
    case ALARM_SOUND_MAPLE_LEAF:
      notes = MELODY_MAPLE_LEAF; count = ARRAY_LENGTH(MELODY_MAPLE_LEAF); break;
    case ALARM_SOUND_RONDO:
      notes = MELODY_RONDO; count = ARRAY_LENGTH(MELODY_RONDO); break;
    case ALARM_SOUND_CLASSIC_RING:
      notes = MELODY_CLASSIC_RING; count = ARRAY_LENGTH(MELODY_CLASSIC_RING); break;
    case ALARM_SOUND_DIGITAL:
      notes = MELODY_DIGITAL; count = ARRAY_LENGTH(MELODY_DIGITAL); break;
    case ALARM_SOUND_SUNRISE:
      notes = MELODY_SUNRISE; count = ARRAY_LENGTH(MELODY_SUNRISE); break;
    case ALARM_SOUND_SIREN:
      notes = MELODY_SIREN; count = ARRAY_LENGTH(MELODY_SIREN); break;
    case ALARM_SOUND_CHIRP:
      notes = MELODY_CHIRP; count = ARRAY_LENGTH(MELODY_CHIRP); break;
    case ALARM_SOUND_PULSE:
      notes = MELODY_PULSE; count = ARRAY_LENGTH(MELODY_PULSE); break;
    case ALARM_SOUND_DAYBREAK:
      notes = MELODY_DAYBREAK; count = ARRAY_LENGTH(MELODY_DAYBREAK); break;
    case ALARM_SOUND_BREEZE:
      notes = MELODY_BREEZE; count = ARRAY_LENGTH(MELODY_BREEZE); break;
    case ALARM_SOUND_BLOOM:
      notes = MELODY_BLOOM; count = ARRAY_LENGTH(MELODY_BLOOM); break;
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
      s_volume = prv_is_rising(sound) ? VOLUME_RISE_START : VOLUME_NORMAL;
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
