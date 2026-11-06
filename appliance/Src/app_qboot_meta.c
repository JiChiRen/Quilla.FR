/**
  ******************************************************************************
  * @file    app_qboot_meta.c
  * @brief   FreeRTOS APP 侧的 QBOOT META 交互标志写入。
  *
  * @note    记录格式、CRC、追加策略与 quilla 的 quilla-qboot-meta.c 二进制兼容。
  *          只实现 APP 需要的「改写 flag」这一条路径，不实现 floor/seal/manifest 的
  *          业务写入；这些字段在追加记录时原样继承，避免升级触发动作把出厂
  *          manifest 抹掉（QBOOT_CHECK 无 manifest 会拒绝跳 APP）。
  *
  *          写策略：读两个 slot 找最大 seq 的有效记录 -> 在活动 slot 的写头追加
  *          一条 seq+1 的新记录 -> 回读校验。活动 slot 写满时先擦除备用 slot 再切换，
  *          因此持有唯一有效副本的 slot 在任何时刻都不会被擦除；写入过程中掉电，
  *          QBOOT 仍能回落到上一条完整记录。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app_qboot_meta.h"
#include "py32f0xx_hal.h"

#include <string.h>

/* Private define ------------------------------------------------------------*/
/* ---- 区域几何（与 quilla-qboot-hal.h / quilla-qboot-meta.h 一致） ---- */
#define QB_META_BASE        0x08006000U
#define QB_META_SIZE        0x1000U
#define QB_SLOT_SIZE        (QB_META_SIZE / 2U)
#define QB_SLOTS            2U
#define QB_RECORD_SIZE      256U
#define QB_REC_PER_SLOT     (QB_SLOT_SIZE / QB_RECORD_SIZE)
#define QB_PAGE_SIZE        256U                 /* PY32F040 擦除页 = 256 B */

/* ---- 记录内块布局 ---- */
#define QB_META_BLK_MAGIC   0x514D544CU         /* "QMTL" */
#define QB_MANI_BLK_MAGIC   0x514D4631U         /* "QMF1" */
#define QB_MANI_BLK_OFF     64U
#define QB_MANI_BLK_SIZE    128U

#define QB_OFF_MAGIC        0U
#define QB_OFF_SEQ          4U
#define QB_OFF_FLAG         8U
#define QB_OFF_FLOOR        12U
#define QB_OFF_CRC          52U

#define QB_MANI_OFF_MAGIC     0U
#define QB_MANI_OFF_VERSION   4U
#define QB_MANI_OFF_IMAGE_LEN 8U
#define QB_MANI_OFF_CRC       108U

/* Private variables ---------------------------------------------------------*/
/* HAL_FLASH_PageProgram 要求源缓冲 32 位对齐：用 uint32_t 数组声明，
   armcc 与 GCC 都无需编译器扩展。两块共 512 B，常驻 .bss。 */
static uint32_t s_best_words[QB_RECORD_SIZE / 4U];
static uint32_t s_work_words[QB_RECORD_SIZE / 4U];

/* Private function prototypes -----------------------------------------------*/
static uint32_t QB_Rd32(const uint8_t *p);
static void     QB_Wr32(uint8_t *p, uint32_t v);
static uint32_t QB_Crc32(const uint8_t *p, uint32_t len);
static void     QB_FlashRead(uint32_t addr, uint8_t *dst, uint32_t len);
static uint8_t  QB_RegionErased(uint32_t addr, uint32_t len);
static int      QB_FlashErasePages(uint32_t base, uint32_t pages);
static int      QB_FlashProgramPage(uint32_t addr, const uint8_t *src);
static uint32_t QB_RecordSeq(uint32_t addr, uint8_t *scratch);
static uint8_t  QB_ManifestBlockValid(const uint8_t *rec);
static void     QB_BuildEmptyManifest(uint8_t *rec);

/* Private functions ---------------------------------------------------------*/
static uint32_t QB_Rd32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void QB_Wr32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 0);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

/* IEEE 802.3 CRC-32（反射，多项式 0xEDB88320），与 quilla meta_crc32 相同 */
static uint32_t QB_Crc32(const uint8_t *p, uint32_t len)
{
  uint32_t crc = 0xFFFFFFFFU;
  uint32_t i;

  for(i = 0U; i < len; i++)
  {
    uint32_t j;
    crc ^= (uint32_t)p[i];
    for(j = 0U; j < 8U; j++)
    {
      crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320U) : (crc >> 1U);
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

static void QB_FlashRead(uint32_t addr, uint8_t *dst, uint32_t len)
{
  const volatile uint8_t *src = (const volatile uint8_t *)(uintptr_t)addr;
  uint32_t i;

  for(i = 0U; i < len; i++)
  {
    dst[i] = src[i];
  }
}

static uint8_t QB_RegionErased(uint32_t addr, uint32_t len)
{
  uint32_t off;

  for(off = 0U; off < len; off += 4U)
  {
    uint32_t w;
    QB_FlashRead(addr + off, (uint8_t *)&w, 4U);
    if(w != 0xFFFFFFFFU)
    {
      return 0U;
    }
  }
  return 1U;
}

static int QB_FlashErasePages(uint32_t base, uint32_t pages)
{
  FLASH_EraseInitTypeDef erase;
  uint32_t page_error = 0U;
  HAL_StatusTypeDef st;

  if(((base % QB_PAGE_SIZE) != 0U) || (pages == 0U))
  {
    return -1;
  }
  if(HAL_FLASH_Unlock() != HAL_OK)
  {
    return -1;
  }

  erase.TypeErase   = FLASH_TYPEERASE_PAGEERASE;
  erase.PageAddress = base;
  erase.NbPages     = pages;
  st = HAL_FLASH_Erase(&erase, &page_error);

  (void)HAL_FLASH_Lock();
  return (st == HAL_OK) ? 0 : -2;
}

/* 只在已擦除页上编程。HAL_FLASH_PageProgram 遇到「目标位未擦除」不会报错，
   所以编程后必须逐字节回读，否则脏页会伪装成一次成功提交。 */
static int QB_FlashProgramPage(uint32_t addr, const uint8_t *src)
{
  HAL_StatusTypeDef st;
  uint32_t i;

  if((addr % QB_PAGE_SIZE) != 0U)
  {
    return -1;
  }
  if(HAL_FLASH_Unlock() != HAL_OK)
  {
    return -1;
  }

  st = HAL_FLASH_PageProgram(addr, (uint32_t *)(uintptr_t)src);

  (void)HAL_FLASH_Lock();

  if(st != HAL_OK)
  {
    return -3;
  }
  for(i = 0U; i < QB_PAGE_SIZE; i++)
  {
    uint8_t rb = 0U;
    QB_FlashRead(addr + i, &rb, 1U);
    if(rb != src[i])
    {
      return -4;
    }
  }
  return 0;
}

/* 读一条记录到 scratch，meta 块 magic/CRC/seq 均合法时返回其 seq，否则 0 */
static uint32_t QB_RecordSeq(uint32_t addr, uint8_t *scratch)
{
  uint32_t seq;

  QB_FlashRead(addr, scratch, QB_RECORD_SIZE);

  if(QB_Rd32(scratch + QB_OFF_MAGIC) != QB_META_BLK_MAGIC)
  {
    return 0U;
  }
  if(QB_Crc32(scratch, QB_OFF_CRC) != QB_Rd32(scratch + QB_OFF_CRC))
  {
    return 0U;
  }

  seq = QB_Rd32(scratch + QB_OFF_SEQ);
  return (seq == 0U) ? 0U : seq;   /* 0 是「空记录」哨兵 */
}

static uint8_t QB_ManifestBlockValid(const uint8_t *rec)
{
  const uint8_t *p = rec + QB_MANI_BLK_OFF;

  if(QB_Rd32(p + QB_MANI_OFF_MAGIC) != QB_MANI_BLK_MAGIC)
  {
    return 0U;
  }
  if(QB_Crc32(p, QB_MANI_OFF_CRC) != QB_Rd32(p + QB_MANI_OFF_CRC))
  {
    return 0U;
  }
  return 1U;
}

/* 构造「无有效 manifest」的 QMF1 块，字段与 quilla meta_append 的默认值一致 */
static void QB_BuildEmptyManifest(uint8_t *rec)
{
  uint8_t *p = rec + QB_MANI_BLK_OFF;

  (void)memset(p, 0xFFU, QB_MANI_BLK_SIZE);
  QB_Wr32(p + QB_MANI_OFF_MAGIC,     QB_MANI_BLK_MAGIC);
  QB_Wr32(p + QB_MANI_OFF_VERSION,   0U);
  QB_Wr32(p + QB_MANI_OFF_IMAGE_LEN, 0U);
  QB_Wr32(p + QB_MANI_OFF_CRC,       QB_Crc32(p, QB_MANI_OFF_CRC));
}

/* Exported functions --------------------------------------------------------*/
int AppQBoot_MetaSetFlag(uint32_t flag)
{
  uint8_t  *best = (uint8_t *)s_best_words;
  uint8_t  *work = (uint8_t *)s_work_words;
  uint32_t head[QB_SLOTS] = { 0U, 0U };
  uint32_t best_seq = 0U;
  uint8_t  best_slot = 0U;
  uint8_t  found = 0U;
  uint8_t  slot;
  uint8_t  active;
  uint32_t idx;
  uint32_t base;
  uint32_t off;
  uint32_t new_seq;
  int      rc;

  /* 1) 扫描两个 slot：各自向前扫到第一个空洞；跨 slot 取 seq 最大者为权威记录 */
  for(slot = 0U; slot < QB_SLOTS; slot++)
  {
    for(idx = 0U; idx < QB_REC_PER_SLOT; idx++)
    {
      uint32_t addr = QB_META_BASE + ((uint32_t)slot * QB_SLOT_SIZE) +
                      (idx * QB_RECORD_SIZE);
      uint32_t seq = QB_RecordSeq(addr, work);

      if(seq == 0U)
      {
        break;
      }
      head[slot] = idx + 1U;
      if((found == 0U) || (seq > best_seq))
      {
        best_seq  = seq;
        best_slot = slot;
        found     = 1U;
        (void)memcpy(best, work, QB_RECORD_SIZE);
      }
    }
  }

  active  = (found != 0U) ? best_slot : 0U;
  new_seq = best_seq + 1U;              /* 空存储时首条记录 seq = 1 */

  /* 2) 活动 slot 写满：切到备用 slot 并整槽擦除。
        备用槽不是权威记录所在槽，擦除不会丢掉唯一有效副本；
        新记录写完后它才成为新的权威记录。 */
  if(head[active] >= QB_REC_PER_SLOT)
  {
    uint8_t other = (uint8_t)(1U - (uint32_t)active);
    rc = QB_FlashErasePages(QB_META_BASE + ((uint32_t)other * QB_SLOT_SIZE),
                            QB_SLOT_SIZE / QB_PAGE_SIZE);
    if(rc != 0)
    {
      return rc;
    }
    active = other;
    head[active] = 0U;
  }

  base = QB_META_BASE + ((uint32_t)active * QB_SLOT_SIZE);
  off  = head[active] * QB_RECORD_SIZE;

  /* 3) 组合新记录：继承上一条的全部字段，只改 seq / flag 并重算 meta CRC。
        manifest 块独立带 CRC，校验通过就原样复制（保住出厂 manifest）。 */
  if(found != 0U)
  {
    (void)memcpy(work, best, QB_RECORD_SIZE);
  }
  else
  {
    (void)memset(work, 0xFFU, QB_RECORD_SIZE);
    /* floor / seal_hash / seal_len 清零，与 quilla 空存储默认值一致 */
    (void)memset(work + QB_OFF_FLOOR, 0U, QB_OFF_CRC - QB_OFF_FLOOR);
  }

  QB_Wr32(work + QB_OFF_MAGIC, QB_META_BLK_MAGIC);
  QB_Wr32(work + QB_OFF_SEQ,   new_seq);
  QB_Wr32(work + QB_OFF_FLAG,  flag);
  QB_Wr32(work + QB_OFF_CRC,   QB_Crc32(work, QB_OFF_CRC));

  if((found == 0U) || (QB_ManifestBlockValid(work) == 0U))
  {
    QB_BuildEmptyManifest(work);
  }

  /* 4) 目标页必须是干净页；若残留旧数据，只擦这一页（写头处无有效记录）。
        追加写依赖「只能把 1 变 0」，脏页会静默写坏记录。 */
  if(QB_RegionErased(base + off, QB_PAGE_SIZE) == 0U)
  {
    rc = QB_FlashErasePages(base + off, 1U);
    if(rc != 0)
    {
      return rc;
    }
  }

  rc = QB_FlashProgramPage(base + off, work);
  if(rc != 0)
  {
    return rc;
  }

  /* 5) 回读并按 QBOOT 的读取规则复验，确认提交真的可被识别 */
  if(QB_RecordSeq(base + off, best) != new_seq)
  {
    return -5;
  }
  if(QB_Rd32(best + QB_OFF_FLAG) != flag)
  {
    return -6;
  }
  return 0;
}

void AppQBoot_Reboot(void)
{
  __disable_irq();
  NVIC_SystemReset();
  for(;;)
  {
  }
}
