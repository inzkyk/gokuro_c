#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wshadow"
#endif

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#if __clang__
#pragma clang diagnostic pop
#endif

typedef uint8_t bool;
#define true 1
#define false 0

#include "buffer.h"

static bool begin_with(const char *a, const char *b) {
  uint32_t i = 0;
  while (true) {
    if (a[i] == '\0' || b[i] == '\0') {
      return true;
    }

    if (a[i] != b[i]) {
      return false;
    }

    i++;
  }
}

static bool end_with(const char *a, const char c) {
  uint32_t i = 0;
  bool just_read_c = false;
  while (true) {
    if (a[i] == '\0') {
      return just_read_c;
    }
    just_read_c = (a[i] == c);
    i++;
  }
}

typedef struct {
  uint32_t is_valid;
  uint32_t body_offset;
} macro_def;

typedef struct {
  uint64_t key;
  macro_def value;
} macro_hash_t;

static uint64_t hash(void *data_begin, void *data_end)
{
  uint64_t hash = 1099511628211u;
  for (unsigned char *c = data_begin; c < (unsigned char *)data_end; c++)
    {
      hash = hash * 33 + *c;
    }
  return hash;
}

int main() {
  const uint32_t buffer_initial_size = 4;
  buffer_t line_buf = {0};
  buffer_init(&line_buf, buffer_initial_size);
  if (line_buf.data == NULL) {
    return 1;
  }

  buffer_t temp_buf = {0};
  buffer_init(&temp_buf, buffer_initial_size);
  if (temp_buf.data == NULL) {
    return 1;
  }

  buffer_t macro_names = {0};
  buffer_init(&macro_names, buffer_initial_size);
  if (macro_names.data == NULL) {
    return 1;
  }

  buffer_t macro_bodies = {0};
  buffer_init(&macro_bodies, buffer_initial_size);
  if (macro_bodies.data == NULL) {
    return 1;
  }

  time_t t = time(NULL);
  stbds_rand_seed((uint32_t)(t));

  macro_hash_t *macros = NULL;

  while (true) {
    buffer_get_line(&line_buf, stdin);
    if (line_buf.capacity == 0) {
      break;
    }

    if (!end_with(line_buf.data, '\n')) {
      // This line is the last line of the input.
      line_buf.data[line_buf.used - 1] = '\n'; // '\0' -> '\n'
      buffer_put(&line_buf, "", 1);
    }

    char *line_begin = line_buf.data;
    char *line_end = line_buf.data + line_buf.used - 2; // 2 = strlen("\n\0")

    if (begin_with(line_begin, "#+MACRO: ")) {
      // This line may define a macro.
      const uint32_t name_offset = 9; // 9 = strlen("#+MACRO: ")

      if (*(line_begin + name_offset) == '\n') {
        break;
      }

      char *name_begin = line_begin + name_offset;
      char *name_end = NULL;
      { // read the name of the macro.
        char *c = name_begin;
        while (true) {
          if (*c == ' ') {
            name_end = c;
            break;
          }
          if (*c == '\n') {
            // This line does not define a macro.
            break;
          }
          c++;
        }

        if (name_end == NULL) {
          fprintf(stdout, "%s", line_begin);
          continue;
        }
      }

      buffer_put_until(&macro_names, name_begin, name_end);
      buffer_put(&macro_names, "", 1);

      buffer_put_until(&macro_bodies, name_end + 1, line_end);
      buffer_put(&macro_bodies, "", 1);

      uint32_t body_length = (uint32_t)(line_end - name_end) - 1; // 1 = strlen(" ")
      macro_def m = {true, macro_bodies.used - body_length - 1}; // 1 = strlen("\0")
      uint64_t macro_name_hash = hash(name_begin, name_end);
      hmput(macros, macro_name_hash, m);

      fprintf(stdout, "%s", line_begin);
      continue;
    }

    // macro expansion
    while (true) {
      const uint32_t bbb_offset = 3; // 3 = strlen("{{{") or strlen("}}}")
      line_begin = line_buf.data;
      line_end = line_buf.data + line_buf.used - 2; // 2 = strlen("\n\0")

      char *macro_begin = NULL;
      char *macro_end = NULL;
      { // find the last macro call.
        char *c = line_end; // *c == "\n"
        while (true) {
          if (c < line_begin) {
            break;
          }
          if (*c == '{') {
            c--;
            if (*c == '{') {
              c--;
              if (*c == '{') {
                macro_begin = c;
                break;
              }
            }
          } else if (*c == '}') {
            c--;
            if (*c == '}') {
              c--;
              if (*c == '}') {
                macro_end = c + 3;
                c--;
                continue;
              }
            }
          } else {
            c--;
          }
        }

        if (macro_begin == NULL || macro_end == NULL) {
          // no macro to expand.
          break;
        }
      }

      char *name_begin = macro_begin + bbb_offset;
      char *name_end = NULL;
      bool isConstant;
      { // get the length of the macro name.
        char *c = name_begin;
        while (true) {
          if (*c == '(') {
            isConstant = false;
            break;
          }

          if (*c == '}') {
            isConstant = true;
            break;
          }
          c++;
        }
        name_end = c;
      }

      // lookup the definition of the macro.
      uint64_t name_hash = hash(name_begin, name_end);
      macro_def m = hmget(macros, name_hash);

      if (!m.is_valid) {
        // the macro is undefined.
        buffer_shrink_to(&line_buf, macro_begin);
        buffer_put_until(&line_buf, macro_end, line_end + 2); // 2 = strlen("\n\0")
        continue;
      }

      if (isConstant) {
        // temp_buf = line[macro_end:]
        buffer_clear(&temp_buf);
        buffer_put_until(&temp_buf, macro_end, line_end + 2); // 2 = strlen("\n\0")

        // line = line[:macro_begin] + macro_body + line[maro_end:]
        buffer_shrink_to(&line_buf, macro_begin);
        buffer_put_string(&line_buf, macro_bodies.data + m.body_offset);
        buffer_copy(&line_buf, &temp_buf);
      } else {
        // parse the arguments.
        char *args[11] = {0};
        args[0] = name_end + 1; // 1 = strlen("(")
        args[1] = name_end + 1; // 1 = strlen("(")
        uint32_t currentIndex = 2;
        for (char *c = args[1]; c < macro_end; c++) {
          if (currentIndex == 10) {
            break;
          }
          if (*c == ',') {
            args[currentIndex] = c + 1; // 1 = strlen(",")
            currentIndex++;
          }
        }
        args[currentIndex] = macro_end - bbb_offset;

        // Expand the macro on temp_buf (we cannot modify line_buf because args depends on it).
        buffer_clear(&temp_buf);
        buffer_put_until(&temp_buf, line_begin, macro_begin);
        char *writeFrom = macro_bodies.data + m.body_offset;
        char *c = macro_bodies.data + m.body_offset;
        while (true) {
          if (*c == '\0') {
            buffer_put_until(&temp_buf, writeFrom, c);
            break;
          }
          if (*c == '$') {
            // If *c != '\n', we can read *(c + 1).
            c++;
            if ('0' <= *c && *c <= '9') {
              uint32_t offset = 1; // 1 = strlen("$")
              buffer_put_until(&temp_buf, writeFrom, c - offset);
              uint32_t argIndex = (uint32_t)(*c - '0');
              char *writeUntil;
              if (argIndex == 0) {
                // The replacement is the whole text in the brackets.
                writeUntil = macro_end - bbb_offset - 1; // 1 = strlen(")")
              } else {
                writeUntil = args[argIndex + 1] - 1; // 1 = strlen(",") or strlen(")")
              }
              buffer_put_until(&temp_buf, args[argIndex], writeUntil);
              c++;
              writeFrom = c;
              continue;
            }
          }
          c++;
        }

        // copy the expanded line to line_buf.
        buffer_put_until(&temp_buf, macro_end, line_end + 2); // 2 = strlen("\n\0")
        line_buf.used = 0;
        buffer_copy(&line_buf, &temp_buf);
      }
    }
    fprintf(stdout, "%s", line_begin);
  }

  free(macro_names.data);
  free(macro_bodies.data);
  free(line_buf.data);
  free(temp_buf.data);
  hmfree(macros);
  return 0;
}
