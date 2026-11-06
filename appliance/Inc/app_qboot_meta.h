/**
  ******************************************************************************
  * @file    app_qboot_meta.h
  * @brief   APP 侧 QBOOT META 交互标志读写接口。
  *
  * @note    本模块只做一件事：把 QBOOT/APP 交互标志改写为 QBOOT，然后复位。
  *          记录格式必须与 quilla 的 quilla-qboot-meta.c / .h 保持二进制兼容，
  *          否则 QBOOT 启动时会把 META 判为空白并回落到默认的 APP1。
  *          本模块不引入 Contiki / quilla 运行时依赖，可独立编译进 FreeRTOS APP。
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef APP_QBOOT_META_H
#define APP_QBOOT_META_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 与 quilla-qboot-flag.h 保持一致，不要另起一套取值 */
#define APP_QBOOT_FLAG_APP1    0x00000001U
#define APP_QBOOT_FLAG_QIAP    0x00000002U
#define APP_QBOOT_FLAG_QBOOT   0x00000003U

/**
  * @brief  向 META 追加一条把 flag 改为指定值的新记录。
  * @param  flag: APP_QBOOT_FLAG_*
  * @retval 0 成功；<0 Flash 失败（调用方必须视为提交失败，不得复位升级）
  * @note   追加写、不覆盖当前有效记录；manifest / floor / seal 等字段原样保留。
  */
int AppQBoot_MetaSetFlag(uint32_t flag);

/**
  * @brief  系统复位（不返回）。
  */
void AppQBoot_Reboot(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_QBOOT_META_H */
