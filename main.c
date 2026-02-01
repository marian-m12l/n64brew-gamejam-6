#include <string.h>
#include <stdint.h>
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/tpx.h>
#include "game_state.h"
#include "gfx.h"
#include "logo.h"
#include "persistence.h"
#include "recovery.h"
#include "pc64.h"


// FIXME debug heaps
static char __attribute__((aligned(16))) heaps_buf[40];


#define FB_COUNT (3)
#define OFFSCREEN_SIZE (80)
#define MUSIC_CHANNEL (4)
#define SFX_CHANNEL (0)
#define FONT_HALODEK (2)

static T3DViewport viewport;
static T3DVec3 camPos = {{ 0.0f, 70.0f, 120.0f }};
static T3DVec3 camTarget = {{0,0,0}};
static uint8_t colorAmbient[4] = {80, 80, 100, 0xFF};
static uint8_t colorDir[4]     = {0xEE, 0xAA, 0xAA, 0xFF};
static T3DVec3 lightDirVec = {{-1.0f, 1.0f, 1.0f}};
static int frameIdx = 0;
static float frametime;
static float gtime;

static T3DModel* console_model;
static T3DModel* n64_model;

static sprite_t* bg_pattern;
static sprite_t* bg_gradient;

static xm64player_t music;
static wav64_t sfx_blip;
static wav64_t sfx_attack;
static wav64_t sfx_whoosh;
static wav64_t sfx_crt_off;
static wav64_t sfx_gameover;


static sprite_t* logo_n64;
static sprite_t* logo_saturn;
static sprite_t* logo_playstation;

static sprite_t* spr_a;
static sprite_t* spr_b;
static sprite_t* spr_c_up;
static sprite_t* spr_c_down;
static sprite_t* spr_progress;
static sprite_t* spr_circlemask;
static sprite_t* spr_reset;
static sprite_t* spr_power;

static sprite_t* spr_swirl;

static rdpq_font_t *font_halo_dek;
static rdpq_textparms_t textparms = { .align = ALIGN_CENTER, .width = 320, };
static rdpq_textparms_t descparms = { .align = ALIGN_CENTER, .width = 280, .height = 120, .wrap = WRAP_WORD };


static int current_joypad = -1;
static float holding = 0.0f;
static uint32_t held_ms;
static reset_type_t rst;
static bool wrong_joypads_count = false;
static bool paused = false;
static bool in_reset = false;


// These variables keep their value during a reset, so we can measure reset time and
// apply gameplay to the console the player was plugged into when the reset button was hit

static volatile int reset_console __attribute__((section(".persistent")));
static volatile uint32_t reset_ticks __attribute__((section(".persistent")));



// Callback for NMI/Reset interrupt

static void reset_interrupt_callback(void) {
	// Keep track of the time the player pressed the reset button
	reset_ticks = TICKS_READ() | 1;
	if (global_state.game_state == IN_GAME) {
		// Keep track of the current console
		reset_console = current_joypad;
		// Play sound effect
		wav64_play(&sfx_crt_off, SFX_CHANNEL);
	}
	in_reset = true;
}


// Console setup

// This is a callback for t3d_model_draw_custom, it is used when a texture in a model is set to dynamic/"reference"
void dynamic_tex_cb(void* userData, const T3DMaterial* material, rdpq_texparms_t *tileParams, rdpq_tile_t tile) {
  if(tile != TILE0)return; // this callback can happen 2 times per mesh, you are allowed to skip calls

  surface_t *offscreenSurf = (surface_t*)userData;
  rdpq_sync_tile();

  int sHalf = OFFSCREEN_SIZE / 2;
  int sFull = OFFSCREEN_SIZE;

  // upload a slice of the offscreen-buffer, the screen in the TV model is split into 4 materials for each section
  // if you are working with a small enough single texture, you can ofc use a normal sprite upload.
  // the quadrant is determined by the texture reference set in fast64, which can be used as an arbitrary value
  switch(material->textureA.texReference) { // Note: TILE1 is used here due to CC shenanigans
    case 1: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  0,     0,     sHalf, sHalf); break;
    case 2: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  sHalf, 0,     sFull, sHalf); break;
    case 3: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  0,     sHalf, sHalf, sFull); break;
    case 4: rdpq_tex_upload_sub(TILE1, offscreenSurf, NULL,  sHalf, sHalf, sFull, sFull); break;
  }
}

static void setup_console(int i, console_t* console) {
	displayable_t* displayable = console->displayable;
	displayable->model = console_model;
	displayable->mat_fp = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
	displayable->skel = t3d_skeleton_create_buffered(displayable->model, 1 /* FIXME FB_COUNT*/);
	displayable->bone = t3d_skeleton_find_bone(&displayable->skel, "console");
	displayable->offscreen_surf = surface_alloc(FMT_RGBA16, OFFSCREEN_SIZE, OFFSCREEN_SIZE);
	displayable->offscreen_surf_z = surface_alloc(FMT_RGBA16, OFFSCREEN_SIZE, OFFSCREEN_SIZE);
	rspq_block_begin();
		t3d_model_draw_custom(displayable->model, (T3DModelDrawConf){
			.userData = &displayable->offscreen_surf,
			.dynTextureCb = dynamic_tex_cb,
			.matrices = displayable->skel.bufferCount == 1
				? displayable->skel.boneMatricesFP
				: (const T3DMat4FP*)t3d_segment_placeholder(T3D_SEGMENT_SKELETON)
		});
	displayable->dpl = rspq_block_end();

	displayable->model2 = n64_model;
	displayable->mat_fp2 = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
	rspq_block_begin();
		t3d_model_draw(displayable->model2);
	displayable->dpl2 = rspq_block_end();
	// Particles
	particles_t* particles = &console_particles[i];
	uint32_t allocSize = sizeof(TPXParticleS8) * PARTICLE_COUNT_MAX / 2;
	particles->buffer = malloc_uncached(allocSize);
	memset(particles->buffer, 0, allocSize);
	particles->mat_fp = malloc_uncached(sizeof(T3DMat4FP) * FB_COUNT);
}


// Level setup

const float console_scales[MAX_CONSOLES] = { 0.18f, 0.18f, 0.13f, 0.10f };
const T3DVec3 console_positions[MAX_CONSOLES][MAX_CONSOLES] = {
	{ (T3DVec3){{0, 0, -25.0f}},		(T3DVec3){{0, 0, 0}}, 				(T3DVec3){{0, 0, 0}}, 				(T3DVec3){{0, 0, 0}} },
	{ (T3DVec3){{-40.0f, 0, -25.0f}}, 	(T3DVec3){{40.0f, 0, -25.0f}},		(T3DVec3){{0, 0, 0}}, 				(T3DVec3){{0, 0, 0}} },
	{ (T3DVec3){{-50.0f, 0, -10.0f}}, 	(T3DVec3){{0, 0, -40.0f}}, 			(T3DVec3){{50.0f, 0, -10.0f}},		(T3DVec3){{0, 0, 0}} },
	{ (T3DVec3){{-50.0f, 0, 10.0f}}, 	(T3DVec3){{-22.0f, 0, -40.0f}},		(T3DVec3){{22.0f, 0, -40.0f}}, 		(T3DVec3){{50.0f, 0, 10.0f}} }
};
const T3DVec3 console_rotations[MAX_CONSOLES][MAX_CONSOLES] = {
	{ (T3DVec3){{0, 0, 0}},							(T3DVec3){{0, 0, 0}}, 						(T3DVec3){{0, 0, 0}}, 						(T3DVec3){{0, 0, 0}} },
	{ (T3DVec3){{0, T3D_DEG_TO_RAD(-10.0f), 0}}, 	(T3DVec3){{0, T3D_DEG_TO_RAD(10.0f), 0}},	(T3DVec3){{0, 0, 0}}, 						(T3DVec3){{0, 0, 0}} },
	{ (T3DVec3){{0, T3D_DEG_TO_RAD(-30.0f), 0}}, 	(T3DVec3){{0, 0, 0}}, 						(T3DVec3){{0, T3D_DEG_TO_RAD(30.0f), 0}},	(T3DVec3){{0, 0, 0}} },
	{ (T3DVec3){{0, T3D_DEG_TO_RAD(-45.0f), 0}}, 	(T3DVec3){{0, T3D_DEG_TO_RAD(-10.0f), 0}},	(T3DVec3){{0, T3D_DEG_TO_RAD(10.0f), 0}}, 	(T3DVec3){{0, T3D_DEG_TO_RAD(45.0f), 0}} }
};

void load_level(int next_level) {
	debugf_uart("Loading level %d\n", next_level);
	const level_t* level = &levels[next_level];

	debugf_uart("Initializing %d consoles\n", level->consoles_count);
	for (int i=0; i<level->consoles_count; i++) {
		console_t* console = add_console();
		setup_console(i, console);
		// Position consoles
		float scale = console_scales[level->consoles_count-1];
		console->scale = (T3DVec3){{ scale, scale, scale }};
		console->rotation = console_rotations[level->consoles_count-1][i];
		console->position = console_positions[level->consoles_count-1][i];
		replicate_console(console);
	}
}

void clear_level() {
	debugf_uart("Clearing level %d\n", global_state.current_level);
	const level_t* level = &levels[global_state.current_level];
	debugf_uart("Erasing %d/%d consoles\n", consoles_count, level->consoles_count);
	int count = consoles_count;
	for (int i=0; i<count; i++) {
		console_t* console = &consoles[i];
		erase_and_free_replicas(console->replicas, CONSOLE_REPLICAS);
		displayable_t* displayable = console->displayable;
		free_uncached(displayable->mat_fp);
		t3d_skeleton_destroy(&displayable->skel);
		surface_free(&displayable->offscreen_surf);
		surface_free(&displayable->offscreen_surf_z);
		rspq_block_free(displayable->dpl);
		free_uncached(displayable->mat_fp2);
		rspq_block_free(displayable->dpl2);
		memset(displayable, 0, sizeof(displayable_t));
		memset(console, 0, sizeof(console_t));
		particles_t* particles = &console_particles[i];
		free_uncached(particles->buffer);
		free_uncached(particles->mat_fp);
		memset(particles, 0, sizeof(particles_t));

		attacker_t* attacker = &console_attackers[i];
		erase_and_free_replicas(attacker->replicas, ATTACKER_REPLICAS);
		memset(attacker, 0, sizeof(attacker_t));

		overheat_t* overheat = &console_overheat[i];
		erase_and_free_replicas(overheat->replicas, OVERHEAT_REPLICAS);
		memset(overheat, 0, sizeof(overheat_t));

		consoles_count--;
	}
}


// Music playback

static void stop_music() {
	if (music.playing) {
		xm64player_stop(&music);
		xm64player_close(&music);
	}
}

static void play_ingame_music() {
	stop_music();
	xm64player_open(&music, "rom:/flyaway.xm64");
    xm64player_set_loop(&music, true);
    xm64player_set_vol(&music, 0.30);
	xm64player_play(&music, MUSIC_CHANNEL);
}

static void play_menu_music() {
	stop_music();
    xm64player_open(&music, "rom:/inmemory.xm64");
    xm64player_set_loop(&music, true);
    xm64player_set_vol(&music, 0.55);
	xm64player_play(&music, MUSIC_CHANNEL);
}


// Game logic loop

void update() {
	t3d_viewport_set_projection(&viewport, T3D_DEG_TO_RAD(45.0f), 10.0f, 150.0f);
	t3d_viewport_look_at(&viewport, &camPos, &camTarget, &(T3DVec3){{0,1,0}});

	switch (global_state.game_state) {
		case INTRO: {
			// Only accept first controller to start the game
			if (current_joypad == 0) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				if (pressed.a || pressed.start) {
					set_game_state(NEXT_LEVEL);
					reset_level_global_state(global_state.current_level);
					//play_menu_music();
					wav64_play(&sfx_blip, SFX_CHANNEL);
				}
				if (global_state.games_count > 0 && pressed.z) {
					reset_level_global_state(5);
					load_level(5);
					play_ingame_music();
					set_game_state(IN_GAME);
					set_practice(true);
					wav64_play(&sfx_blip, SFX_CHANNEL);
				}
			}
			break;
		}
		case IN_GAME: {
			set_level_timer(global_state.level_timer - frametime);
			bool cleared = (global_state.level_timer < 0.0f);

			// Spawn attackers and add attacks
			const level_t* level = &levels[global_state.current_level];
			for (int i=0; i<consoles_count; i++) {
				console_t* console = &consoles[i];
				attacker_t* attacker = &console_attackers[i];
				overheat_t* overheat = &console_overheat[i];
				if (!attacker->spawned || (attacker->level < QUEUE_LENGTH && attacker->last_attack - level->attack_grace_pediod >= global_state.level_timer)) {
					float r = rand() / (float) RAND_MAX;
					float threshold = frametime * level->attack_rate;
					float max_time_between_attacks = 2.0f * (1.0f / level->attack_rate);
					if (r < threshold || attacker->last_attack - global_state.level_timer >= max_time_between_attacks) {
						wav64_play(&sfx_attack, SFX_CHANNEL);
						if (!attacker->spawned) {
							spawn_attacker(i);
						} else if (attacker->last_attack - level->attack_grace_pediod >= global_state.level_timer) {
							grow_attacker(i);
						}
					}
				}
				bool overheating = attacker->spawned && attacker->level == QUEUE_LENGTH;
				if (overheating && overheat->last_overheat - global_state.level_timer >= OVERHEAT_PERIOD) {
					wav64_play(&sfx_whoosh, SFX_CHANNEL);
					increase_overheat(i);
					// Game over if reached level 4
					if (console_overheat[i].overheat_level > 3) {
						debugf_uart("OVERHEAT GAME OVER %d\n", i);
						clear_level();
						wav64_play(&sfx_gameover, SFX_CHANNEL);
						play_menu_music();
						set_game_state(GAME_OVER);
						set_game_over(OVERHEATED);
					}
				}
			}

			// Handle inputs
			if (current_joypad != -1) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				attacker_t* attacker = &console_attackers[current_joypad];
				if (attacker->spawned && attacker->level > 0) {
					queue_button_t btn = get_attacker_button(current_joypad, 0);
					joypad_buttons_t down = joypad_get_buttons(current_joypad);
					bool held = false;
					switch (btn) {
						case BTN_A:
							held = down.a && !down.b && !down.c_up && !down.c_down;
							break;
						case BTN_B:
							held = down.b && !down.a && !down.c_up && !down.c_down;
							break;
						case BTN_C_UP:
							held = down.c_up && !down.a && !down.b && !down.c_down;
							break;
						case BTN_C_DOWN:
							held = down.c_down && !down.a && !down.b && !down.c_up;
							break;
					}
					if (held) {
						holding += frametime;
						if (holding >= BUTTON_HOLD_THRESHOLD) {	// TODO Threshold depending on enemy strength
							shrink_attacker(current_joypad);
							holding = 0;
						}
					} else {
						holding = 0;
					}
				}

#ifdef DEBUG_MODE
				// Debug commands
				if (pressed.r) {
					// Spawn attacker
					int idx = current_joypad;
					if (!console_attackers[idx].spawned) {
						spawn_attacker(idx);
					} else {
						grow_attacker(idx);
					}
				}
				if (pressed.l) {
					// Shrink attacker
					int idx = current_joypad;
					if (console_attackers[idx].spawned) {
						shrink_attacker(idx);
					}
				}
				if (pressed.d_up) {
					// Increase heat
					int idx = current_joypad;
					increase_overheat(idx);
					// Game over if reached level 4
					if (console_overheat[idx].overheat_level > 3) {
						debugf_uart("OVERHEAT GAME OVER %d\n", idx);
						clear_level();
						wav64_play(&sfx_gameover, SFX_CHANNEL);
						play_menu_music();
						set_game_state(GAME_OVER);
						set_game_over(OVERHEATED);
					}
				}
				if (pressed.d_down) {
					// Decrease heat
					int idx = current_joypad;
					if (console_overheat[idx].overheat_level > 0) {
						decrease_overheat(idx);
					}
				}
				if (pressed.start) {
					cleared = true;
				}
#endif

				if (global_state.practice && pressed.z) {
					set_game_state(INTRO);
					clear_level();
					reset_global_state();
					play_menu_music();
					wav64_play(&sfx_blip, SFX_CHANNEL);
				}
			}

			// Handle end condition and change level
			if (!global_state.practice && cleared) {
				set_game_state(NEXT_LEVEL);
				// TODO Keep level displayed for a few seconds, clear when loading the next level
				clear_level();
				reset_level_global_state(global_state.current_level + 1);
				play_menu_music();
				wav64_play(&sfx_blip, SFX_CHANNEL);
			}
			break;
		}
		case NEXT_LEVEL: {
			if (current_joypad != -1) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				if (pressed.a || pressed.start) {
					// Load next level
					int next_level = global_state.current_level;
					if (next_level < TOTAL_LEVELS) {
						load_level(next_level);
						play_ingame_music();
						set_game_state(IN_GAME);
					} else {
						wav64_play(&sfx_blip, SFX_CHANNEL);
						set_game_state(FINISHED);
					}
				}
			}
			break;
		}
		case FINISHED: {
			if (current_joypad != -1) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				if (pressed.start) {
					reset_global_state();
					wav64_play(&sfx_blip, SFX_CHANNEL);
				}
			}
			break;
		}
		case GAME_OVER: {
			if (current_joypad != -1) {
				joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
				if (pressed.start) {
					reset_global_state();
					wav64_play(&sfx_blip, SFX_CHANNEL);
				}
			}
			break;
		}
	}
}


// Render to console screens

void render_offscreen() {
	if (global_state.game_state == IN_GAME) {
		for (int i=0; i<consoles_count; i++) {
			console_t* console = &consoles[i];
			// ======== Draw (Offscreen) ======== //
			// Render the offscreen-scene first, for that we attach the extra buffer instead of the screen one
			rdpq_attach_clear(&console->displayable->offscreen_surf, &console->displayable->offscreen_surf_z);

			attacker_t* attacker = &console_attackers[i];
			int x = 0;
			int y = 0;
			float s = 1.0f - (.25f * attacker->level);
			if (s > 0.0f) {
				rdpq_set_mode_standard();
				rdpq_sprite_blit(logo_n64, x, y, &(rdpq_blitparms_t) {
					.scale_x = s, .scale_y = s,
				});
			}
			if (attacker->spawned && attacker->level > 0) {
				// Draw attacker logo (size grows with attacker level)
				x = 80 - 20 * attacker->level;
				y = 80 - 20 * attacker->level;
				s = .25f * attacker->level;
				sprite_t* spr = NULL;
				switch (attacker->rival_type) {
					case SATURN:
						spr = logo_saturn;
						break;
					case PLAYSTATION:
						spr = logo_playstation;
						break;
				}
				rdpq_set_mode_standard();
				rdpq_sprite_blit(spr, x, y, &(rdpq_blitparms_t) {
					.scale_x = s, .scale_y = s,
				});
			}

			if (i == reset_console) {
				draw_bars(exception_reset_time()/2);
			}

			rdpq_detach();
		}
	}
}


// 3D scene render

void render_3d() {
	t3d_frame_start();
	t3d_viewport_attach(&viewport);

	t3d_screen_clear_color(RGBA32(80, 80, 80, 255));
	t3d_screen_clear_depth();
	
	t3d_light_set_ambient(colorAmbient);
	t3d_light_set_directional(0, colorDir, &lightDirVec);
	t3d_light_set_count(1);

	switch (global_state.game_state) {
		case INTRO:
			draw_bg(bg_pattern, bg_gradient, gtime * 4.0f, RGBA32(0xff, 0xaa, 0x66, 0xff));
			break;
		case IN_GAME: {
			draw_bg(bg_pattern, bg_gradient, gtime * 4.0f, RGBA32(0xcc*0.75f, 0xcc*0.75f, 0xff, 0xff));
			for (int i=0; i<consoles_count; i++) {
				console_t* console = &consoles[i];
				t3d_skeleton_update(&console->displayable->skel);
				t3d_mat4fp_from_srt_euler(
					&console->displayable->mat_fp[frameIdx],
					console->scale.v,
					console->rotation.v,
					console->position.v
				);
				t3d_matrix_push(&console->displayable->mat_fp[frameIdx]);
      			t3d_skeleton_use(&console->displayable->skel);

				overheat_t* overheat = &console_overheat[i];
				float noise_strength = 0.2f * (1 + overheat->overheat_level);

				if (i == reset_console) {
					// Quickly remove noise on the console's screen
					noise_strength -= noise_strength * exception_reset_time() / RESET_TIME_LENGTH;
					if (noise_strength < 0)	noise_strength = 0;
				}

				// CRT model uses primary color to blend texture and noise
				uint8_t blend = (uint8_t)(noise_strength * 255.4f);
    			rdpq_set_prim_color(RGBA32(blend, blend, blend, 255 - blend));
				rspq_block_run(console->displayable->dpl);
				
				if(console->displayable->bone >= 0) {
  					rdpq_mode_push();
					float s = 4.0f;
					t3d_mat4fp_from_srt_euler(
						&console->displayable->mat_fp2[frameIdx],
						//console->scale.v,
						(float[3]){ console->scale.x * s, console->scale.y * s, console->scale.z * s },
						(float[3]){ 0.0f, 0.0f, 0.0f },
						(float[3]){ 0.0f, 0.0f, 0.0f }
					);
					// to attach another model, simply use a bone form the skeleton:
					t3d_matrix_push(&console->displayable->skel.boneMatricesFP[console->displayable->bone]);
					t3d_matrix_push(&console->displayable->mat_fp2[frameIdx]); // apply local matrix of the model
					if (current_joypad == i) {
						rdpq_set_prim_color(RGBA32(0, 200, 0, 255));
					} else {
						rdpq_set_prim_color(RGBA32(0, 0, 0, 255));
					}
					rspq_block_run(console->displayable->dpl2);
					rdpq_sync_pipe();

					// Particles
					if (overheat->overheat_level > 0) {
						T3DVec3 offset = (T3DVec3){{0, 0, -10.0f}};
						drawsmoke(&console_particles[i], offset, console_scales[consoles_count-1], frameIdx, frametime, overheat->overheat_level, spr_swirl);
					}
					rdpq_sync_pipe();

					t3d_matrix_pop(2);
  					rdpq_mode_pop();
				}
				t3d_matrix_pop(1);
			}
			break;
		}
		case NEXT_LEVEL:
			draw_bg(bg_pattern, bg_gradient, gtime * 4.0f, RGBA32(0xcc*0.75f, 0xcc*0.75f, 0xff, 0xff));
			break;
		case FINISHED:
			draw_bg(bg_pattern, bg_gradient, gtime * 4.0f, RGBA32(0xcc*0.75f, 0xff, 0xcc*0.75f, 0xff));
			break;
		case GAME_OVER:
			draw_bg(bg_pattern, bg_gradient, gtime * 4.0f, RGBA32(0xff, 0xcc*0.75f, 0xcc*0.75f, 0xff));
			break;
	}
}


// 2D overlay render

void render_2d() {
	rdpq_sync_pipe();

#ifdef DEBUG_MODE
	heap_stats_t stats;
	sys_get_heap_stats(&stats);
	int heap_size = stats.total;
	if (heap_size > 4*1024*1024) {
		heap_size -= 4*1024*1024;
	}
	heaps_stats(heaps_buf, 40);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 150, "Reset console : %d", reset_console);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 160, "    Boot type : %s", rst == RESET_COLD ? "COLD" : "WARM");
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 170, "     Restored : %d/%d/%d/%d", restored_global_state_count, restored_consoles_count, restored_attackers_count, restored_overheat_count);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 180, "       Resets : %ld/%d-%d-%d-%d", global_state.reset_count, global_state.level_reset_count_per_console[0], global_state.level_reset_count_per_console[1], global_state.level_reset_count_per_console[2], global_state.level_reset_count_per_console[3]);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 190, " Power cycles : %ld/%d", global_state.power_cycle_count, global_state.level_power_cycle_count);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 200, "         Heap : %d/%d", stats.used, heap_size);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 16, 210, "  Heaps stats : %s", heaps_buf);

	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 150, "State     : %d", global_state.game_state);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 160, "Level     : %d", global_state.current_level);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 170, "Ignored   : %d/%d", restored_attackers_ignored, restored_overheat_ignored);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 180, "Port      : %d", current_joypad);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 190, "Reset held: %ldms", held_ms);
	rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 200, 200, "FPS   : %.2f", display_get_fps());
#endif

	switch (global_state.game_state) {
		case INTRO:
        	rdpq_text_printf(&textparms, FONT_HALODEK, 0, 60, "CONSOLE");
        	rdpq_text_printf(&textparms, FONT_HALODEK, 0, 100, "CLASH");
			if (current_joypad != 0) {
				rdpq_text_printf(&descparms, FONT_BUILTIN_DEBUG_MONO, 20, 110, "Please make sure to plug a single controller to the first port");
			}
			if (global_state.games_count > 0) {
				rdpq_text_printf(&descparms, FONT_BUILTIN_DEBUG_MONO, 20, 130, "Press Z to practice");
			}
			break;
		case IN_GAME: {
			const level_t* level = &levels[global_state.current_level];

			if (wrong_joypads_count) {
				rdpq_text_printf(&textparms, FONT_BUILTIN_DEBUG_MONO, 0, 110, "No really, use a single controller!");
			}

			for (int i=0; i<consoles_count; i++) {
				console_t* console = &consoles[i];
				attacker_t* attacker = &console_attackers[i];
				overheat_t* overheat = &console_overheat[i];

				T3DVec3 billboardPos = (T3DVec3){{
					console->position.v[0] - 140 * console->scale.x,
					console->position.v[1] + 200 * console->scale.x,
					console->position.v[2]
				}};
				T3DVec3 billboardScreenPos;
				t3d_viewport_calc_viewspace_pos(&viewport, &billboardScreenPos, &billboardPos);
				int x = floorf(billboardScreenPos.v[0]);
				int y = floorf(billboardScreenPos.v[1]);
				float s = 5 * console_scales[level->consoles_count-1];

				if (attacker->spawned && attacker->level > 0) {
					// Draw queue
					for (int j=0; j<attacker->level; j++) {
						int btn_x = x + (j * 32 * s);
						queue_button_t btn = get_attacker_button(i, j);
						if (i == current_joypad && j == 0) {
							drawprogress(btn_x - (8*s), y - (8*s), s, holding/BUTTON_HOLD_THRESHOLD, RGBA32(255, 0, 0, 255), spr_progress, spr_circlemask);
						}
						sprite_t* spr = NULL;
						switch (btn) {
							case BTN_A:
								spr = spr_a;
								break;
							case BTN_B:
								spr = spr_b;
								break;
							case BTN_C_UP:
								spr = spr_c_up;
								break;
							case BTN_C_DOWN:
								spr = spr_c_down;
								break;
						}
						rdpq_mode_begin();
							rdpq_set_mode_standard();
							rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
						rdpq_mode_end();
						rdpq_sprite_blit(spr, btn_x, y, &(rdpq_blitparms_t) {
							.scale_x = s, .scale_y = s,
						});
					}
				}

				if (global_state.practice) {
					// Attacker decay
					draw_gauge(x-10, y+25, 6, 1, 0, 1, restored_attackers_counts[i], ATTACKER_REPLICAS,
						restored_attackers_counts[i] >= restored_attackers_minimas[i] ? RGBA32(0x00, 0xff , 0, 0xff) : RGBA32(0xff, 0 , 0, 0xff),
						RGBA32(0, 0, 0, 0xc0)
					);
					rdpq_set_prim_color(RGBA32(0xff, 0xff, 0xff, 0xff));
					rdpq_fill_rectangle(x-10+1+restored_attackers_minimas[i], y+25, x-10+1+restored_attackers_minimas[i]+1, y+31);
					// Overheat decay
					draw_gauge(x-10, y+45, 6, 1, 0, 1, restored_overheat_counts[i], OVERHEAT_REPLICAS,
						restored_overheat_counts[i] >= restored_overheat_minimas[i] ? RGBA32(0x00, 0xff , 0, 0xff) : RGBA32(0xff, 0 , 0, 0xff),
						RGBA32(0, 0, 0, 0xc0)
					);
					rdpq_set_prim_color(RGBA32(0xff, 0xff, 0xff, 0xff));
					rdpq_fill_rectangle(x-10+1+restored_overheat_minimas[i], y+45, x-10+1+restored_overheat_minimas[i]+1, y+51);

					rdpq_sync_pipe();
					rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y+20, "Attacker decay:");
					rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y+40, "Overheat decay:");
				}
#ifdef DEBUG_MODE
				rdpq_sync_pipe();
				rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y+20, "%d/%d/%d", restored_attackers_counts[i], restored_attackers_minimas[i], restored_attackers_ignored);
				rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y+30, "%d/%f", attacker->level, attacker->last_attack);
				rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y+40, "%d/%d/%d", restored_overheat_counts[i], restored_overheat_minimas[i], restored_overheat_ignored);
				rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, x, y+50, "%d/%f", overheat->overheat_level, overheat->last_overheat);
#endif

				// Reset and overheat gauges (per console)
				x = i * 80;
				rdpq_mode_begin();
					rdpq_set_mode_standard();
					rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
				rdpq_mode_end();
				rdpq_sprite_blit(spr_swirl, x + 8, 220, &(rdpq_blitparms_t) {
					.width = 32, .height = 32,
					.scale_x = 0.5f, .scale_y = 0.5f,
				});
				bool overheating = attacker->spawned && attacker->level == QUEUE_LENGTH;
				draw_gauge(x + 26, 225, 6, 5, 0, 1, overheat->overheat_level, 3,
					RGBA32(0xff, 0xc0 - 0x60 * (overheat->overheat_level - 1), 0, 0xff),
					overheating ? RGBA32((int) fabs((fmodf((overheat->last_overheat - global_state.level_timer) * (overheat->overheat_level + 1), 2.0f) - 1) * 0xff), 0, 0, 0xff) : RGBA32(0, 0, 0, 0xc0)
				);
				if (level->max_resets_per_console > 0) {
					rdpq_mode_begin();
						rdpq_set_mode_standard();
						rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
					rdpq_mode_end();
					rdpq_sprite_blit(spr_reset, x + 44, 220, NULL);
					draw_gauge(x + 58, 225, 6, 5, 1, 1, level->max_resets_per_console - global_state.level_reset_count_per_console[i], level->max_resets_per_console, RGBA32(0xff, 0xff, 0xff, 0xff), RGBA32(0, 0, 0, 0xc0));
				}
			}

			// Power-off gauge (shared)
			// TODO Right-to-left ?
			if (level->max_power_cycles > 0) {
				draw_gauge(284, 10, 6, 10, 1, 1, level->max_power_cycles - global_state.level_power_cycle_count, level->max_power_cycles, RGBA32(0xff, 0xff, 0xff, 0xff), RGBA32(0, 0, 0, 0xc0));
				rdpq_mode_begin();
					rdpq_set_mode_standard();
					rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
				rdpq_mode_end();
				rdpq_sprite_blit(spr_power, 268, 5, NULL);
			}

			// Timer
			rdpq_sync_pipe();
			if (global_state.practice) {
				rdpq_text_printf(&textparms, FONT_HALODEK, 0, 30, "PRACTICE");
			} else {
        		rdpq_text_printf(&textparms, FONT_HALODEK, 0, 30, "%d", (int) ceilf(global_state.level_timer));
			}
			break;
		}
		case NEXT_LEVEL: {
			if (global_state.current_level == 0) {
        		rdpq_text_printf(&textparms, FONT_HALODEK, 0, 60, "LET'S GO!");
			} else {
        		rdpq_text_printf(&textparms, FONT_HALODEK, 0, 60, "LEVEL %d CLEARED!", global_state.current_level);
			}
			if (global_state.current_level < TOTAL_LEVELS) {
				rdpq_text_printf(&descparms, FONT_BUILTIN_DEBUG_MONO, 20, 110, levels[global_state.current_level].description);
			}
			break;
		}
		case FINISHED: {
        	rdpq_text_printf(&textparms, FONT_HALODEK, 0, 60, "CONGRATULATIONS!");
			break;
		}
		case GAME_OVER: {
        	rdpq_text_printf(&textparms, FONT_HALODEK, 0, 60, "GAME");
        	rdpq_text_printf(&textparms, FONT_HALODEK, 0, 100, "OVER");
			switch (global_state.game_over) {
				case OVERHEATED:
					rdpq_text_printf(&descparms, FONT_BUILTIN_DEBUG_MONO, 20, 110, "Oh no! One of your consoles overheated!");
					break;
				case TOO_MANY_RESETS:
					rdpq_text_printf(&descparms, FONT_BUILTIN_DEBUG_MONO, 20, 110, "Too many resets for this console! Only %d per console in this level!", levels[global_state.current_level].max_resets_per_console);
					break;
				case TOO_MANY_POWER_CYCLES:
					rdpq_text_printf(&descparms, FONT_BUILTIN_DEBUG_MONO, 20, 110, "Too many power cycles! Only %d in this level!", levels[global_state.current_level].max_power_cycles);
					break;
				case PARTIAL_RESTORATION:
					rdpq_text_printf(&descparms, FONT_BUILTIN_DEBUG_MONO, 20, 110, "Oops! You lost your console to memory decay... Don't push it next time!");
					break;
			}
			break;
		}
	}
}


// Game setup, data restoration and main loop

int main(void) {

	// Measure reset time as soon as possible (for warm boot)

	held_ms = TICKS_TO_MS(TICKS_SINCE(reset_ticks));
	reset_ticks = 0;
	rst = sys_reset_type();
	if (rst == RESET_COLD) {
		held_ms = 0;
	}


	// Init systems

	debug_init_isviewer();
	debug_init_usblog();

	debugf_uart("Console Clash\n");

	debugf_uart("Boot type: %s\n", rst == RESET_COLD ? "COLD" : "WARM");
	debugf_uart("held_ms = %ld\n", held_ms);

	asset_init_compression(2);
    wav64_init_compression(3);
	dfs_init(DFS_DEFAULT_LOCATION);
	rdpq_init();
	joypad_init();
    timer_init();
    audio_init(32000, 4);
    mixer_init(32);

	debugf_uart("Init OK\n");

	//rdpq_debug_start();

    // Initialize the random number generator, then call rand() every
    // frame so to get random behavior also in emulators.
    uint32_t seed;
    getentropy(&seed, sizeof(seed));
    srand(seed);
    register_VI_handler((void(*)(void))rand);

	debugf_uart("Seed OK\n");

	// Skip restoration / force cold boot behaviour by holding R+A during startup
	bool forceColdBoot;
	joypad_poll();
	JOYPAD_PORT_FOREACH(port) {
		joypad_buttons_t held = joypad_get_buttons_held(port);
		if (held.a && held.r) {
			debugf_uart("Forcing cold boot and skipping restoration\n");
			forceColdBoot = true;
		}
	}

	debugf_uart("Joypad poll OK\n");

	bool useExpansionPak;
#ifndef NO_EXPANSION_PAK
	useExpansionPak = is_memory_expanded();
#endif
	debugf_uart("Expansion Pak: %d\n", useExpansionPak);
	init_heaps(useExpansionPak);


	// Try to restore game data after a warm or cold boot

	bool restored_something = false;
	if (!forceColdBoot) {
		restored_something = try_recover();
		debugf_uart("Restoration done\n");
	}


	// Clear all replicas to avoid bad data in next restoration

	debugf_uart("Clearing heaps\n");
	clear_heaps();
	debugf_uart("Heaps cleared\n");


	// If initializing game from scratch, display logos

	if (!restored_something) {
		debugf_uart("Entering initial boot sequence\n");
#ifndef DEBUG_MODE
		n64brew_logo();
		libdragon_logo();
#endif

		// Initial setup
		consoles_count = 0;
		reset_console = -1;
		init_global_state();
		debugf_uart("Cold initial sequence OK\n");
	}


	// Load assets

	wav64_open(&sfx_blip, "rom:/blip.wav64");
	wav64_open(&sfx_attack, "rom:/attack.wav64");
	wav64_open(&sfx_whoosh, "rom:/whoosh.wav64");
	wav64_open(&sfx_crt_off, "rom://crt_off.wav64");
	wav64_open(&sfx_gameover, "rom://gameover.wav64");

	console_model = t3d_model_load("rom:/crt.t3dm");
	n64_model = t3d_model_load("rom:/console.t3dm");
	
	bg_pattern = sprite_load("rom:/pattern.i8.sprite");
	bg_gradient = sprite_load("rom:/gradient.i8.sprite");
	
	logo_n64 = sprite_load("rom:/n64.sprite");
	logo_saturn = sprite_load("rom:/saturn.sprite");
	logo_playstation = sprite_load("rom:/playstation.sprite");

    spr_a = sprite_load("rom:/AButton.sprite");
    spr_b = sprite_load("rom:/BButton.sprite");
    spr_c_up = sprite_load("rom:/CUp.sprite");
    spr_c_down = sprite_load("rom:/CDown.sprite");
    spr_progress = sprite_load("rom:/CircleProgress.i8.sprite");
    spr_circlemask = sprite_load("rom:/CircleMask.i8.sprite");
    spr_reset = sprite_load("rom:/reset.sprite");
    spr_power = sprite_load("rom:/power.sprite");

	spr_swirl = sprite_load("rom://swirl.i4.sprite");

    font_halo_dek = rdpq_font_load("rom:/HaloDek.font64");
    rdpq_text_register_font(FONT_HALODEK, font_halo_dek);
    rdpq_font_style(font_halo_dek, 0, &(rdpq_fontstyle_t){.color = RGBA32(0xFF, 0xFF, 0xFF, 0xFF) });
	
	debugf_uart("Resources load OK\n");


	// Setup display and tiny3d
	
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);

	debugf_uart("Display init OK\n");

	t3d_init((T3DInitParams){});
	viewport = t3d_viewport_create_buffered(FB_COUNT);
  	rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

	t3d_vec3_norm(&lightDirVec);

	frameIdx = 0;
	
	debugf_uart("T3D init OK\n");


	tpx_init((TPXInitParams){});
	
	debugf_uart("TPX init OK\n");


	// Initialize game state from scratch or from restored data
	
	if (restored_something) {
		debugf_uart("Entering followup boot sequence\n");
		debugf_uart("restored: %d global state / %d consoles / %d attackers / %d overheat\n", restored_global_state_count, restored_consoles_count, restored_attackers_count, restored_overheat_count);
		
		// Check validity of restored data: game over if broken level
		if (validate_recovered()) {
    		global_state = restored_global_state;
			replicate_global_state();

			debugf_uart("game_state: %d\n", global_state.game_state);
			debugf_uart("reset_console: %d\n", reset_console);

			if (global_state.game_state == IN_GAME) {
				// Restored at least once console: keep playing
				consoles_count = restored_consoles_count;
				for (int i=0; i<restored_consoles_count; i++) {
					uint32_t id = restored_consoles[i].id;
					console_t* console = &consoles[id];
					*console = restored_consoles[i];
					debugf_uart("restored: %d\n", console->id);
					console->displayable = &console_displayables[i];
					// Recreate replicas (alternative would be to keep replicas as-is)
					replicate_console(console);
				}
				debugf_uart("Consoles restored\n");

				for (int i=0; i<restored_overheat_count; i++) {
					uint32_t id = restored_overheat[i].id;
					if (restored_overheat_counts[id] < restored_overheat[i].min_replicas) {
						debugf_uart("overheat #%d restored with not enough replicas (%d<%d): NOT RESTORING\n", id, restored_overheat_counts[id], restored_overheat[i].min_replicas);
						restored_overheat_ignored++;
						continue;
					}
					overheat_t* overheat = &console_overheat[id];
					*overheat = restored_overheat[i];
					debugf_uart("restored overheat: %d\n", overheat->id);
					replicate_overheat(overheat);
				}

				for (int i=0; i<restored_attackers_count; i++) {
					uint32_t id = restored_attackers[i].id;
					if (restored_attackers_counts[id] < restored_attackers[i].min_replicas) {
						debugf_uart("attacker #%d restored with not enough replicas (%d<%d): NOT RESTORING\n", id, restored_attackers_counts[id], restored_attackers[i].min_replicas);
						restored_attackers_ignored++;
						continue;
					}
					attacker_t* attacker = &console_attackers[id];
					*attacker = restored_attackers[i];
					debugf_uart("restored attacker: %d\n", attacker->id);
					if (attacker->spawned) {
						replicate_attacker(attacker);
						// Make sure overheat timer makes sense if it was not restored along with attacker
						if (console_overheat[attacker->id].last_overheat == 0) {
							reset_overheat_timer(attacker->id);
						}
					} else {
						debugf_uart("restored unspawned attacker --> not replicating\n");
					}
				}

				// Load model for each console
				for (int i=0; i<consoles_count; i++) {
					setup_console(i, &consoles[i]);
				}
				
				debugf_uart("Consoles setup OK\n");
			}

			if (rst == RESET_COLD) {
				debugf_uart("Cold\n");
				inc_power_cycle_count();
				if (global_state.game_state == IN_GAME && !global_state.practice) {
					inc_level_power_cycle_count();
					// TODO Handle too many power cycles in level --> game over
					if (global_state.level_power_cycle_count > levels[global_state.current_level].max_power_cycles) {
						debugf_uart("Too many power cycles in level %d: %d > %d\n", global_state.current_level, global_state.level_power_cycle_count, levels[global_state.current_level].max_power_cycles);
						// TODO Game Over --> display reason?
						clear_level();
						wav64_play(&sfx_gameover, SFX_CHANNEL);
						play_menu_music();
						set_game_state(GAME_OVER);
						set_game_over(TOO_MANY_POWER_CYCLES);
					}
				}
			} else {
				debugf_uart("Warm\n");
				inc_reset_count();
				if (global_state.game_state == IN_GAME) {
					bool gameover = false;
					if (!global_state.practice) {
						inc_level_reset_count_per_console(reset_console);
						if (reset_console != -1 && global_state.level_reset_count_per_console[reset_console] > levels[global_state.current_level].max_resets_per_console) {
							debugf_uart("Too many resets for console %d in level %d: %d > %d\n", reset_console, global_state.current_level, global_state.level_reset_count_per_console[reset_console], levels[global_state.current_level].max_resets_per_console);
							clear_level();
							wav64_play(&sfx_gameover, SFX_CHANNEL);
							play_menu_music();
							set_game_state(GAME_OVER);
							set_game_over(TOO_MANY_RESETS);
							gameover = true;
						}
					}
					if (!gameover) {
						for (int i=0; i<consoles_count; i++) {
							overheat_t* overheat = &console_overheat[i];
							if (i == reset_console) {
								// Decrease overheat level of console depending on held_ms
								if (overheat->overheat_level > 0) {
									debugf_uart("DECREASE overheat of RESET CONSOLE: %d\n", i);
									decrease_overheat(i);
									if (levels[global_state.current_level].allow_long_reset && held_ms >= LONG_RESET_THRESHOLD) {
										debugf_uart("DECREASE AGAIN overheat of RESET CONSOLE: %d held=%d\n", i, held_ms);
										decrease_overheat(i);
									}
								}
							} else {
								// For long presses of the reset button, apply attacks/overheat to the other consoles
								if (levels[global_state.current_level].allow_long_reset && held_ms > LONG_RESET_GRACE_PERIOD) {
									float replay_ms = held_ms - LONG_RESET_GRACE_PERIOD;
									attacker_t* attacker = &console_attackers[i];
									bool overheating = attacker->spawned && attacker->level == QUEUE_LENGTH;
									if (overheating) {
										debugf_uart("REPLAY OVERHEAT on console #%d: add %f ms\n", i, replay_ms);
										overheat->last_overheat += replay_ms;
									} else {
										// Lower attack rate
										float factor = 0.5f;
										int attacks = (replay_ms / 1000.0f) * levels[global_state.current_level].attack_rate * factor;
										debugf_uart("REPLAY ATTACKS on console #%d: %f ms -> %d attacks\n", i, replay_ms, attacks);
										for (int j=0; j<attacks; j++) {
											if (!attacker->spawned) {
												spawn_attacker(i);
											} else {
												grow_attacker(i);
											}
										}
									}
								}
							}
						}
					}
				}
			}

			reset_console = -1;

			debugf_uart("Followup boot sequence OK\n");
		} else {
			debugf_uart("partial restoration: game over\n");
			// TODO Game Over --> display reason?
			clear_level();
			wav64_play(&sfx_gameover, SFX_CHANNEL);
			play_menu_music();
			// Initial setup
			consoles_count = 0;
			reset_console = -1;
			init_global_state();
			set_game_state(GAME_OVER);
			set_game_over(PARTIAL_RESTORATION);
		}
	}

	dump_game_state();


	// Start appropriate music
    
	if (global_state.game_state == IN_GAME) {
		play_ingame_music();
	} else {
		play_menu_music();
	}
	
	debugf_uart("Music playing OK\n");
	
	
	// Setup reset IRQ handler to keep track of selected console on reset

	register_RESET_handler(reset_interrupt_callback);
	
	debugf_uart("NMI handler register OK\n");


	// Start gameplay

	update();
	
	debugf_uart("First update OK\n");
	debugf_uart("Entering main loop\n");


	// Main loop

	while (true) {
		frameIdx = (frameIdx + 1) % FB_COUNT;
		frametime = display_get_delta_time();
		gtime += frametime;

		mixer_try_play();

		// Identify the current joypad port and make sure only one is plugged
		int ports = 0;
		JOYPAD_PORT_FOREACH(port) {
			if (joypad_is_connected(port)) {
				ports++;
			}
		}
		wrong_joypads_count = ports > 1;
		current_joypad = -1;
		if (!wrong_joypads_count) {
			JOYPAD_PORT_FOREACH(port) {
				if (current_joypad == -1 && joypad_is_connected(port)) {
					current_joypad = port;
				}
			}
		}
		joypad_poll();

#ifdef DEBUG_MODE
		if (current_joypad != -1) {
			joypad_buttons_t pressed = joypad_get_buttons_pressed(current_joypad);
			if (pressed.c_right) {
				paused = !paused;
			}
		}
#endif

		// Game loop

		if (!paused && !in_reset) {
			update();
			dump_game_state();
		}


		// Render

		render_offscreen();

		rdpq_attach(display_get(), display_get_zbuf());
		render_3d();
		render_2d();
		rdpq_detach_show();
	}
	
	debugf_uart("Out of main loop\n");
	
	// TODO Can we keep displaying while reset is held ?!
	// 	--> show gauge for reset time
	//	--> By writing over the last framebuffer with CPU only ?

	// TODO cf. pifhang ??
	
	return 0;
}
