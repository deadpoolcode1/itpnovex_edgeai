/* Host-runnable test for the registry's version-upgrade path (ScopusQA #27).
 *
 * The bug this pins down: `_reg_load()` reads sizeof(t_registry) bytes out of
 * flash, which on a version bump is MORE than was ever written there, and the
 * integrity check then hashed all of them. The bytes past the stored end are
 * whatever the erase left behind, so the CRC could not match, so every version
 * bump took the RESET branch and returned the unit to factory defaults. The
 * migration code below that check had never run.
 *
 * That is the operator's notification mask, detection profile, server
 * endpoints and image settings, gone on a firmware update that was only
 * supposed to add a field.
 *
 * The test manufactures an "old" store by hand: the previous version number,
 * the previous size, a CRC over the previous number of bytes, and 0xFF (the
 * erased state) after it. A correct implementation migrates it and keeps every
 * old field; the broken one resets.
 *
 *   gcc -Wall -Wextra -O2 \
 *       -I vendor/n6cam.core.bsp/Firmware/Application/Core/Inc \
 *       -I vendor/n6cam.core.bsp/Firmware/Middlewares/SIANA/slib32 \
 *       tests/c/test_registry_migration.c \
 *       vendor/n6cam.core.bsp/Firmware/Middlewares/SIANA/slib32/slib32_registry.c \
 *       -o /tmp/test_registry_migration && /tmp/test_registry_migration
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "slib32_registry.h"

/* Same shape as the store's private header. Kept here rather than exported
 * because the point of the test is to write the bytes a PREVIOUS firmware
 * would have written, which is not something the library should offer. */
typedef struct { uint32_t version; uint32_t size; uint32_t crc; } t_meta;

/* The pretend flash sector. Erased state is 0xFF, as the real one is. */
static uint8_t _flash[8192];

static int32_t _read(uint8_t *buff, size_t size)
{
  if (size > sizeof(_flash)) return -1;
  memcpy(buff, _flash, size);
  return (int32_t)size;
}

static int32_t _write(const uint8_t *buff, size_t size)
{
  if (size > sizeof(_flash)) return -1;
  memcpy(_flash, buff, size);
  return (int32_t)size;
}

/* Any 32-bit hash will do: the test only needs the same function on both
 * sides of the write, which is also all the firmware needs. */
static uint32_t _crc32(const uint8_t *buff, size_t size)
{
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; i++)
  {
    c ^= buff[i];
    for (int k = 0; k < 8; k++)
    {
      c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
  }
  return ~c;
}

static int _fails;

static void check(const char *what, bool ok)
{
  printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
  if (!ok) _fails++;
}

int main(void)
{
  /* An older firmware's store: one field shorter than today's struct.
   *
   * `detect_rotate` is the field ScopusQA #26 added, and it is the last one,
   * so "the previous version" is exactly today's struct minus its size. If a
   * later change adds another field this stays correct without editing. */
  const size_t old_data = sizeof(t_registry_data) - sizeof(uint32_t);

  const t_registry_data *defaults = registry_get_defaults();
  t_registry_data old = *defaults;
  /* Values an operator would have set, chosen so a reset is unmistakable:
   * these are the two ScopusQA #27 was reported against. */
  old.notify_enable_mask = 0x55u;
  old.detect_det_mask    = 0x03u;
  old.detect_action_mask = 0x07u;
  old.img_quality        = 71u;
  old.notify_period_s    = 900u;

  memset(_flash, 0xFF, sizeof(_flash));
  t_meta m;
  m.version = REGISTRY_VERSION - 1u;
  m.size    = (uint32_t)(sizeof(t_meta) + old_data);
  m.crc     = _crc32((const uint8_t *)&old, old_data);
  memcpy(_flash, &m, sizeof(m));
  memcpy(_flash + sizeof(m), &old, old_data);

  if (registry_init(_read, _write, _crc32) != SLIB32_OK)
  {
    printf("registry_init failed\n");
    return 1;
  }

  int32_t rc = registry_start(true);
  check("an older store migrates instead of resetting",
        rc == SLIB32_UPDATED);

  t_registry_data *now = registry_acquire();
  if (now == NULL)
  {
    printf("registry_acquire returned NULL\n");
    return 1;
  }

  check("notify_enable_mask survived the upgrade",
        now->notify_enable_mask == 0x55u);
  check("detect_det_mask survived the upgrade",
        now->detect_det_mask == 0x03u);
  check("detect_action_mask survived the upgrade",
        now->detect_action_mask == 0x07u);
  check("img_quality survived the upgrade", now->img_quality == 71u);
  check("notify_period_s survived the upgrade", now->notify_period_s == 900u);
  check("the field the new version added came up as its default",
        now->detect_rotate == defaults->detect_rotate);
  registry_release();

  /* And the store on "flash" is now the new version, so the next boot is a
   * plain load rather than another migration. */
  memcpy(&m, _flash, sizeof(m));
  check("the migrated store was written back at the new version",
        m.version == REGISTRY_VERSION);
  check("the migrated store was written back at the new size",
        m.size == sizeof(t_meta) + sizeof(t_registry_data));

  rc = registry_start(true);
  check("a store at the current version loads without resetting",
        rc == SLIB32_OK);
  now = registry_acquire();
  check("and still carries the operator's mask",
        (now != NULL) && (now->notify_enable_mask == 0x55u));
  if (now) registry_release();

  /* A genuinely corrupt store must still reset: the fix must not turn the
   * integrity check into a no-op. */
  memset(_flash, 0xFF, sizeof(_flash));
  m.version = REGISTRY_VERSION;
  m.size    = (uint32_t)(sizeof(t_meta) + sizeof(t_registry_data));
  m.crc     = 0xDEADBEEFu;
  memcpy(_flash, &m, sizeof(m));
  memcpy(_flash + sizeof(m), &old, sizeof(t_registry_data));
  rc = registry_start(true);
  check("a store with a wrong CRC still resets to defaults",
        rc == SLIB32_RESET);
  now = registry_acquire();
  check("and the defaults are what came back",
        (now != NULL) &&
        (now->notify_enable_mask == defaults->notify_enable_mask));
  if (now) registry_release();

  printf("\n%s\n", _fails ? "FAILURES" : "all checks passed");
  return _fails ? 1 : 0;
}
