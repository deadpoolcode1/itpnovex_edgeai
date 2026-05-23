/* Host-runnable test for hdlc.{c,h}. Builds with the native gcc — no
 * stm32/threadx headers leak through hdlc.c so this is portable.
 *
 *   gcc -Wall -Wextra -O2 \
 *       -I vendor/n6cam.core.bsp/Firmware/Application/Core/Inc \
 *       tests/c/test_hdlc.c \
 *       vendor/n6cam.core.bsp/Firmware/Application/Core/Src/hdlc.c \
 *       -o /tmp/test_hdlc
 *
 * Each test writes "<name>\t<hex-encoded wire frame>\n" to stdout. The
 * Python side (tests/test_hdlc_crosscheck.py) re-runs the same payload
 * through tools/hdlc.py and asserts the wire bytes match. */

#include "hdlc.h"
#include <stdio.h>
#include <string.h>

static void print_hex(const uint8_t *buf, size_t n)
{
  for (size_t i = 0; i < n; i++) printf("%02x", buf[i]);
  printf("\n");
}

static void run_case(const char *name, const uint8_t *payload, size_t n)
{
  uint8_t wire[1024];
  size_t wire_len = 0;
  if (!hdlc_encode(payload, n, wire, sizeof(wire), &wire_len))
  {
    printf("ERROR: encode %s overflowed\n", name);
    return;
  }
  printf("%s\t", name);
  print_hex(wire, wire_len);

  /* Round-trip: decode the wire we just produced and verify the payload. */
  t_hdlc_decoder d;
  uint8_t out[1024];
  hdlc_decoder_init(&d, out, sizeof(out));
  size_t got_len = 0;
  for (size_t i = 0; i < wire_len; i++)
  {
    size_t finished = 0;
    hdlc_decoder_feed(&d, wire[i], &finished);
    if (finished) got_len = finished;
  }
  if (got_len != n || memcmp(out, payload, n) != 0)
  {
    printf("ERROR: %s round-trip mismatch (got %zu bytes)\n", name, got_len);
  }
}

int main(void)
{
  run_case("empty", (const uint8_t *)"", 0);
  run_case("at",    (const uint8_t *)"AT\r\n", 4);

  const uint8_t embedded[] = { 0x7E, 0x7D, 0x7E, 0x7D, 'h', 'i' };
  run_case("flags-and-esc", embedded, sizeof(embedded));

  uint8_t fullrange[256];
  for (int i = 0; i < 256; i++) fullrange[i] = (uint8_t)i;
  run_case("0-255", fullrange, sizeof(fullrange));

  const char *urc =
      "+SDVRUPL: PROGR,\"4325412_23052026_111111.rdy\",1\r\n";
  run_case("sdvrupl", (const uint8_t *)urc, strlen(urc));

  return 0;
}
