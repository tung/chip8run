#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sokol/sokol_app.h"
#include "../sokol/sokol_args.h"
#include "../sokol/sokol_fetch.h"
#include "../sokol/sokol_gfx.h"

#include "../sokol/sokol_glue.h"

#define STRINGIFY2(x) #x
#define STRINGIFY(x) STRINGIFY2(x)

#include STRINGIFY(SHADERS_H)

static struct {
  sg_pipeline pipeline;
  sg_bindings bindings;
  sg_pass_action pass_action;
} draw_state;

#define STACK_MAX 16
#define MEMORY_SIZE 4096
#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 32
#define FONT_OFFSET 0x50
#define PROGRAM_OFFSET 0x200

typedef enum {
  VM_INIT,
  VM_RUN,
  VM_WAIT_FOR_KEY,
  VM_STOPPED,
} vm_state;

static struct {
  vm_state state;
  int speed_up;
  int micros; // run instructions while > 0
  int timer_micros; // microseconds until next 60 Hz tick
  uint8_t key_dest; // destination register when waiting for a key
  bool keys[16]; // key press state
  uint16_t pc; // program counter
  uint16_t i; // index register
  uint8_t v[16]; // registers
  uint16_t stack[STACK_MAX];
  uint8_t stack_top;
  uint8_t delay_timer;
  uint8_t sound_timer;
  uint8_t memory[MEMORY_SIZE];
  uint8_t display[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
} vm;

static uint8_t file_buffer[MEMORY_SIZE - PROGRAM_OFFSET];

static void init(void) {
  sg_setup(&(sg_desc){
    .context = sapp_sgcontext(),
  });

  sg_shader shader =
    sg_make_shader(simple_shader_desc(sg_query_backend()));

  // screen quad texture
  draw_state.bindings.fs_images[SLOT_tex] =
    sg_make_image(&(sg_image_desc){
      .width = DISPLAY_WIDTH,
      .height = DISPLAY_HEIGHT,
      .usage = SG_USAGE_DYNAMIC,
      .pixel_format = SG_PIXELFORMAT_R8,
    });

  // screen quad position (vec2) and tex coords (vec2)
  draw_state.bindings.vertex_buffers[0] =
    sg_make_buffer(&(sg_buffer_desc){
      .size = sizeof(float) * 16,
      .usage = SG_USAGE_DYNAMIC,
      .label = "quad-vertices",
    });

  // clang-format off
  uint16_t indices[] = {
    0, 1, 3,
    1, 2, 3,
  };
  // clang-format on
  draw_state.bindings.index_buffer = sg_make_buffer(&(sg_buffer_desc){
    .type = SG_BUFFERTYPE_INDEXBUFFER,
    .data = SG_RANGE(indices),
    .label = "quad-indices",
  });

  draw_state.pipeline = sg_make_pipeline(&(sg_pipeline_desc){
    .shader = shader,
    .index_type = SG_INDEXTYPE_UINT16,
    .layout = {
      .attrs = {
        [ATTR_vs_position].format = SG_VERTEXFORMAT_FLOAT2,
        [ATTR_vs_a_tex_coord].format = SG_VERTEXFORMAT_FLOAT2,
      },
    },
    .label = "quad-pipeline",
  });

  draw_state.pass_action = (sg_pass_action){
    .colors[0] = {
      .action = SG_ACTION_CLEAR,
      .value = { 0.2, 0.2, 0.2, 1.0 },
    },
  };
}

static void run_timers(int micros) {
  vm.timer_micros -= micros;
  if (vm.timer_micros <= 0) {
    if (vm.delay_timer > 0) {
      vm.delay_timer--;
    }
    if (vm.sound_timer > 0) {
      vm.sound_timer--;
    }
    vm.timer_micros += 16667; // 1/60th of a second in microseconds
  }
}

static void run_vm(void) {
#define DELAY(t) \
  do { \
    vm.micros -= t; \
    run_timers(t); \
  } while (0)
#define STOP \
  do { \
    vm.state = VM_STOPPED; \
    draw_state.pass_action.colors[0].value = \
      (sg_color){ 0.5, 0.5, 0.5, 1.0 }; \
    vm.micros = 0; \
    return; \
  } while (0)
#define CHECK_N(where, NX, V) \
  do { \
    if (NX != V) { \
      fprintf(stderr, \
        "[line %d] " where ": " #NX " must be " #V " (" #NX \
        " = %x)\n", \
        __LINE__, NX); \
      STOP; \
    } \
  } while (0)

  vm.micros +=
    (int)(sapp_frame_duration() * 1000000.0) * (vm.speed_up + 1);

  while (vm.micros > 0) {
    // decode two bytes at program counter
    uint8_t n0 = vm.memory[vm.pc] >> 4;
    uint8_t n1 = vm.memory[vm.pc] & 0xf;
    uint8_t n2 = vm.memory[vm.pc + 1] >> 4;
    uint8_t n3 = vm.memory[vm.pc + 1] & 0xf;
    vm.pc += 2;

    switch (n0) {
      case 0x0: {
        CHECK_N("0", n1, 0x0);
        CHECK_N("0", n2, 0xe);
        if (n3 == 0x0) {
          // 00e0 - clear screen
          memset(vm.display, 0, sizeof(vm.display));
          DELAY(109);
        } else if (n3 == 0xe) {
          // 00ee - return
          if (vm.stack_top == 0) {
            fputs("stack underflow\n", stderr);
            STOP;
          }
          vm.pc = vm.stack[--vm.stack_top];
          DELAY(105);
        }
        break;
      }
      case 0x1: {
        // 1nnn - jump nnn
        vm.pc = n1 << 8 | n2 << 4 | n3;
        DELAY(105);
        break;
      }
      case 0x2: {
        // 2nnn - call nnn
        if (vm.stack_top >= STACK_MAX) {
          fputs("stack overflow\n", stderr);
          STOP;
        }
        vm.stack[vm.stack_top++] = vm.pc;
        vm.pc = n1 << 8 | n2 << 4 | n3;
        DELAY(105);
        break;
      }
      case 0x3: {
        // 3xnn - if vx != nn then
        if (vm.v[n1] == (n2 << 4 | n3)) {
          vm.pc += 2;
        }
        DELAY(55);
        break;
      }
      case 0x4: {
        // 4xnn - if vx == nn then
        if (vm.v[n1] != (n2 << 4 | n3)) {
          vm.pc += 2;
        }
        DELAY(55);
        break;
      }
      case 0x5: {
        // 5xy0 - if vx != vy then
        CHECK_N("5", n3, 0x0);
        if (vm.v[n1] == vm.v[n2]) {
          vm.pc += 2;
        }
        DELAY(73);
        break;
      }
      case 0x6: {
        // 6xnn - vx := nn
        vm.v[n1] = n2 << 4 | n3;
        DELAY(27);
        break;
      }
      case 0x7: {
        // 7xnn - vx += nn
        vm.v[n1] += n2 << 4 | n3;
        DELAY(45);
        break;
      }
      case 0x8: {
        switch (n3) {
          case 0x0: {
            // 8xy0 - vx := vy
            vm.v[n1] = vm.v[n2];
            DELAY(200);
            break;
          }
          case 0x1: {
            // 8xy1 - vx |= vy
            vm.v[n1] |= vm.v[n2];
            vm.v[0xf] = 0;
            DELAY(200);
            break;
          }
          case 0x2: {
            // 8xy2 - vx &= vy
            vm.v[n1] &= vm.v[n2];
            vm.v[0xf] = 0;
            DELAY(200);
            break;
          }
          case 0x3: {
            // 8xy3 - vx ^= vy;
            vm.v[n1] ^= vm.v[n2];
            vm.v[0xf] = 0;
            DELAY(200);
            break;
          }
          case 0x4: {
            // 8xy4 - vx += vy
            uint8_t old_x = vm.v[n1];
            vm.v[n1] += vm.v[n2];
            vm.v[0xf] = vm.v[n1] < old_x;
            DELAY(200);
            break;
          }
          case 0x5: {
            // 8xy5 - vx -= vy
            uint8_t borrow = vm.v[n1] >= vm.v[n2];
            vm.v[n1] -= vm.v[n2];
            vm.v[0xf] = borrow;
            DELAY(200);
            break;
          }
          case 0x6: {
            // 8xy6 - vx >>= vy
            uint8_t lsb = vm.v[n2] & 0x01;
            vm.v[n1] = vm.v[n2] >> 1;
            vm.v[0xf] = lsb;
            DELAY(200);
            break;
          }
          case 0x7: {
            // 8xy7 - vx =- vy
            uint8_t borrow = vm.v[n2] >= vm.v[n1];
            vm.v[n1] = vm.v[n2] - vm.v[n1];
            vm.v[0xf] = borrow;
            DELAY(200);
            break;
          }
          case 0xe: {
            // 8xye - vx <<= vy
            uint8_t msb = !!(vm.v[n2] & 0x40);
            vm.v[n1] = vm.v[n2] << 1;
            vm.v[0xf] = msb;
            DELAY(200);
            break;
          }
          default: {
            fprintf(stderr, "%x %x %x %x\n", n0, n1, n2, n3);
            STOP;
          }
        }
        break;
      }
      case 0x9: {
        // 9xy0 - if vx == vy then
        CHECK_N("9", n3, 0x0);
        if (vm.v[n1] != vm.v[n2]) {
          vm.pc += 2;
        }
        DELAY(73);
        break;
      }
      case 0xa: {
        // annn - i := nnn
        vm.i = n1 << 8 | n2 << 4 | n3;
        DELAY(55);
        break;
      }
      case 0xb: {
        // bnnn - jump0 nnn
        vm.pc = n1 << 8 | n2 << 4 | n3;
        vm.pc += vm.v[0];
        DELAY(105);
        break;
      }
      case 0xc: {
        // cxnn - vx := random nn
        vm.v[n1] = random() & 0xff & (n2 << 4 | n3);
        DELAY(164);
        break;
      }
      case 0xd: {
        // dxyn - sprite vx vy n
        uint8_t x = vm.v[n1] & 63;
        uint8_t y = vm.v[n2] & 31;
        uint8_t h = n3;
        vm.v[0xf] = 0;
        for (uint8_t yy = 0; yy < h; ++yy) {
          uint8_t mask = vm.memory[vm.i + yy] >> (x & 7);
          uint8_t* dest =
            vm.display + (((y + yy) & 31) * DISPLAY_WIDTH + x) / 8;
          vm.v[0xf] |= *dest & mask;
          *dest ^= mask;
          if (!!(x & 7)) {
            uint8_t mask2 = vm.memory[vm.i + yy] << (8 - (x & 7));
            uint8_t* dest2 = x < 56 ? dest + 1 : dest - 7;
            vm.v[0xf] |= *dest2 & mask2;
            *dest2 ^= mask2;
          }
        }
        vm.v[0xf] = !!vm.v[0xf];
        DELAY(22734);
        break;
      }
      case 0xe: {
        switch (n2 << 4 | n3) {
          case 0x9e: {
            // ex9e - if vx -key then
            if (vm.keys[vm.v[n1]]) {
              vm.pc += 2;
            }
            DELAY(73);
            break;
          }
          case 0xa1: {
            // exa1 - if vx key then
            if (!vm.keys[vm.v[n1]]) {
              vm.pc += 2;
            }
            DELAY(73);
            break;
          }
          default: {
            fprintf(stderr, "%x %x %x %x\n", n0, n1, n2, n3);
            STOP;
          }
        }
        break;
      }
      case 0xf: {
        switch (n2 << 4 | n3) {
          case 0x07: {
            // fx07 - vx := delay
            vm.v[n1] = vm.delay_timer;
            DELAY(45);
            break;
          }
          case 0x0a: {
            // fx0a - vx := key (wait for key press)
            vm.key_dest = n1;
            vm.state = VM_WAIT_FOR_KEY;
            run_timers(vm.micros);
            vm.micros = 0;
            return;
          }
          case 0x15: {
            // fx15 - delay := vx
            vm.delay_timer = vm.v[n1];
            DELAY(45);
            break;
          }
          case 0x18: {
            // fx18 - buzzer := vx
            vm.sound_timer = vm.v[n1];
            DELAY(45);
            break;
          }
          case 0x1e: {
            // fx1e - i += vx
            vm.i += vm.v[n1];
            DELAY(86);
            break;
          }
          case 0x29: {
            // fx29 - hex vx (set i to hex character)
            vm.i = FONT_OFFSET + vm.v[n1] * 5;
            DELAY(91);
            break;
          }
          case 0x33: {
            // fx33 - bcd vx (decode vx into binary coded decimal)
            vm.memory[vm.i] = vm.v[n1] / 100;
            vm.memory[vm.i + 1] = vm.v[n1] / 10 % 10;
            vm.memory[vm.i + 2] = vm.v[n1] % 10;
            DELAY(927);
            break;
          }
          case 0x55: {
            // fx55 - save vx
            for (uint8_t ii = 0; ii <= n1; ++ii) {
              vm.memory[vm.i + ii] = vm.v[ii];
            }
            vm.i += n1 + 1;
            DELAY(605);
            break;
          }
          case 0x65: {
            // fx65 - load vx
            for (uint8_t ii = 0; ii <= n1; ++ii) {
              vm.v[ii] = vm.memory[vm.i + ii];
            }
            vm.i += n1 + 1;
            DELAY(605);
            break;
          }
          default: {
            fprintf(stderr, "%04d | %x %x %x %x\n", vm.pc - 2, n0, n1,
              n2, n3);
            STOP;
          }
        }
        break;
      }
    }
  }

#undef CHECK_N
}

static void white_noise(void) {
  for (size_t i = 0; i < sizeof(vm.display); ++i) {
    vm.display[i] = random() & 0xff;
  }
}

static void update_display(void) {
  static uint8_t tex[sizeof(vm.display) * 8];

  // Rasterize display into tex.
  for (size_t i = 0; i < sizeof(vm.display); ++i) {
    tex[i * 8 + 0] |= ((vm.display[i] >> 7) & 1) * 0xe0;
    tex[i * 8 + 1] |= ((vm.display[i] >> 6) & 1) * 0xe0;
    tex[i * 8 + 2] |= ((vm.display[i] >> 5) & 1) * 0xe0;
    tex[i * 8 + 3] |= ((vm.display[i] >> 4) & 1) * 0xe0;
    tex[i * 8 + 4] |= ((vm.display[i] >> 3) & 1) * 0xe0;
    tex[i * 8 + 5] |= ((vm.display[i] >> 2) & 1) * 0xe0;
    tex[i * 8 + 6] |= ((vm.display[i] >> 1) & 1) * 0xe0;
    tex[i * 8 + 7] |= ((vm.display[i] >> 0) & 1) * 0xe0;
  }

  sg_update_image(draw_state.bindings.fs_images[SLOT_tex],
    &(sg_image_data){
      .subimage[0][0] = SG_RANGE(tex),
    });

  // Decay tex values to fade them out over time.
  for (size_t i = 0; i < sizeof(tex); ++i) {
    tex[i] -= (!!tex[i]) * 0x20;
  }
}

static void resize_screen_quad(void) {
  float w = 1.0;
  float h = 1.0;

  if (sapp_widthf() < (sapp_heightf() * 2.0)) {
    h = sapp_widthf() / (sapp_heightf() * 2.0);
  } else {
    w = (sapp_heightf() * 2.0) / sapp_widthf();
  }

  // clang-format off
  float vertices[] = {
     w,  h, 1.0, 0.0, // top right
     w, -h, 1.0, 1.0, // bottom right
    -w, -h, 0.0, 1.0, // bottom left
    -w,  h, 0.0, 0.0, // top left
  };
  // clang-format on
  sg_update_buffer(
    draw_state.bindings.vertex_buffers[0], SG_RANGE_REF(vertices));
}

static void frame(void) {
  sfetch_dowork();

  if (vm.state == VM_INIT) {
    white_noise();
  } else if (vm.state == VM_RUN) {
    run_vm();
    if (vm.state == VM_RUN) {
      // brighten letterbox color instead of playing sound
      draw_state.pass_action.colors[0].value = vm.sound_timer
        ? (sg_color){ 0.8, 0.8, 0.8, 1.0 }
        : (sg_color){ 0.2, 0.2, 0.2, 1.0 };
    }
  } else if (vm.state == VM_WAIT_FOR_KEY) {
    run_timers(
      (int)(sapp_frame_duration() * 1000000.0) * (vm.speed_up + 1));
  } else {
    assert(vm.state == VM_STOPPED);
  }

  update_display();
  resize_screen_quad();

  sg_begin_default_pass(
    &draw_state.pass_action, sapp_width(), sapp_height());
  sg_apply_pipeline(draw_state.pipeline);
  sg_apply_bindings(&draw_state.bindings);
  sg_draw(0, 6, 1);
  sg_end_pass();
  sg_commit();
}

static void prepare_program(const void* program_data, size_t size) {
  if (size > MEMORY_SIZE - PROGRAM_OFFSET) {
    draw_state.pass_action.colors[0].value =
      (sg_color){ 1.0, 0.0, 0.0, 1.0 };
    vm.state = VM_INIT;
    return;
  }

  memset(&vm, 0, sizeof(vm));

  static uint8_t font_data[] = {
    0xf0, 0x90, 0x90, 0x90, 0xf0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xf0, 0x10, 0xf0, 0x80, 0xf0, // 2
    0xf0, 0x10, 0xf0, 0x10, 0xf0, // 3
    0x90, 0x90, 0xf0, 0x10, 0x10, // 4
    0xf0, 0x80, 0xf0, 0x10, 0xf0, // 5
    0xf0, 0x80, 0xf0, 0x90, 0xf0, // 6
    0xf0, 0x10, 0x20, 0x40, 0x40, // 7
    0xf0, 0x90, 0xf0, 0x90, 0xf0, // 8
    0xf0, 0x90, 0xf0, 0x10, 0xf0, // 9
    0xf0, 0x90, 0xf0, 0x90, 0x90, // A
    0xe0, 0x90, 0xe0, 0x90, 0xe0, // B
    0xf0, 0x80, 0x80, 0x80, 0xf0, // C
    0xe0, 0x90, 0x90, 0x90, 0xe0, // D
    0xf0, 0x80, 0xf0, 0x80, 0xf0, // E
    0xf0, 0x80, 0xf0, 0x80, 0x80, // F
  };
  memcpy(vm.memory + FONT_OFFSET, font_data, sizeof(font_data));

  memcpy(vm.memory + PROGRAM_OFFSET, program_data, size);

  vm.pc = PROGRAM_OFFSET;
  vm.state = VM_RUN;
}

static void fetch_callback(const sfetch_response_t* response) {
  if (response->fetched) {
    prepare_program(response->data.ptr, response->data.size);
    if (vm.state == VM_RUN) {
      printf("Loaded %s\n", response->path);

      char title[128];
      const char *file_name = response->path;
      const char* c = strpbrk(file_name, "/\\");
      while (c) {
        file_name = c + 1;
        c = strpbrk(file_name, "/\\");
      }
      snprintf(title, sizeof(title), "%s - chip8run", file_name);
      sapp_set_window_title(title);
    }
  } else if (response->failed) {
    draw_state.pass_action.colors[0].value =
      (sg_color){ 1.0, 0.0, 0.0, 1.0 };
  }
}

#if defined(__EMSCRIPTEN__)
static void html5_file_drop_callback(
    const sapp_html5_fetch_response* response) {
  if (response->succeeded) {
    prepare_program(response->data.ptr, response->data.size);
  } else {
    draw_state.pass_action.colors[0].value =
      (sg_color){ 1.0, 0.0, 0.0, 1.0 };
  }
}
#endif

static void load_file(const char* path) {
  printf("Loading %s ...\n", path);
  sfetch_send(&(sfetch_request_t){
    .path = path,
    .callback = fetch_callback,
    .buffer = SFETCH_RANGE(file_buffer),
    .user_data = { 0 },
  });
}

static uint8_t key_code_to_hex(sapp_keycode key_code) {
  switch (key_code) {
    case SAPP_KEYCODE_UP: return 0x5;
    case SAPP_KEYCODE_LEFT: return 0x7;
    case SAPP_KEYCODE_DOWN: return 0x8;
    case SAPP_KEYCODE_RIGHT: return 0x9;
    case SAPP_KEYCODE_SPACE: return 0x6;
    case SAPP_KEYCODE_ENTER: return 0x6;
    case SAPP_KEYCODE_BACKSPACE: return 0x4;
    case SAPP_KEYCODE_X: return 0x0;
    case SAPP_KEYCODE_1: return 0x1;
    case SAPP_KEYCODE_2: return 0x2;
    case SAPP_KEYCODE_3: return 0x3;
    case SAPP_KEYCODE_Q: return 0x4;
    case SAPP_KEYCODE_W: return 0x5;
    case SAPP_KEYCODE_E: return 0x6;
    case SAPP_KEYCODE_A: return 0x7;
    case SAPP_KEYCODE_S: return 0x8;
    case SAPP_KEYCODE_D: return 0x9;
    case SAPP_KEYCODE_Z: return 0xa;
    case SAPP_KEYCODE_C: return 0xb;
    case SAPP_KEYCODE_4: return 0xc;
    case SAPP_KEYCODE_R: return 0xd;
    case SAPP_KEYCODE_F: return 0xe;
    case SAPP_KEYCODE_V: return 0xf;
    default: return 16;
  }
}

static void event(const sapp_event* e) {
  if (e->type == SAPP_EVENTTYPE_KEY_DOWN) {
    if (e->key_code == SAPP_KEYCODE_ESCAPE) {
      sapp_request_quit();
    } else if (e->key_code == SAPP_KEYCODE_EQUAL) {
      if (vm.speed_up < 7) {
        vm.speed_up++;
      }
    } else if (e->key_code == SAPP_KEYCODE_MINUS) {
      if (vm.speed_up > 0) {
        vm.speed_up--;
      }
    } else {
      uint8_t hex = key_code_to_hex(e->key_code);
      if (hex < 16) {
        vm.keys[hex] = true;
      }
    }
  } else if (e->type == SAPP_EVENTTYPE_KEY_UP) {
    uint8_t hex = key_code_to_hex(e->key_code);
    if (hex < 16) {
      vm.keys[hex] = false;
      if (vm.state == VM_WAIT_FOR_KEY) {
        vm.v[vm.key_dest] = hex;
        vm.state = VM_RUN;
      }
    }
  } else if (e->type == SAPP_EVENTTYPE_FILES_DROPPED) {
#if defined(__EMSCRIPTEN__)
    sapp_html5_fetch_dropped_file(&(sapp_html5_fetch_request){
      .dropped_file_index = 0,
      .callback = html5_file_drop_callback,
      .buffer = SAPP_RANGE(file_buffer),
    });
#else
    load_file(sapp_get_dropped_file_path(0));
#endif
  }
}

static void cleanup(void) {
  sargs_shutdown();
  sfetch_shutdown();
  sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
  puts("build: " STRINGIFY(BUILDSTAMP));

  sargs_setup(&(sargs_desc){
    .argc = argc,
    .argv = argv,
  });

  sfetch_setup(&(sfetch_desc_t){ 0 });

  if (sargs_exists("file")) {
    load_file(sargs_value("file"));
  }

  vm.state = VM_INIT;

  return (sapp_desc){
    .init_cb = init,
    .frame_cb = frame,
    .event_cb = event,
    .cleanup_cb = cleanup,
    .width = 768,
    .height = 480,
    .icon.sokol_default = true,
    .window_title = "chip8run",
    .enable_dragndrop = true,
  };
}
