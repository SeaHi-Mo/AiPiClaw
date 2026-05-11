/**
 * @file axk_skill_loader.h
 * @brief skill_loader module - 安信可科技 BL618 port
 * @note support builtin 技能register& /spiffs/skills/ directory 下 .md file动态load
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_SKILL_LOADER_H
#define __AXK_SKILL_LOADER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief initskill loader，register built-in skill and scan filesystemloadexternal skills
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_skill_loader_init(void);

/**
 * @brief check 文本is否trigger 某 skills
 * @param[in] text input 文本
 * @return trigger 技能name ，not trigger return NULL
 */
const char *axk_skill_match(const char *text);

/**
 * @brief 执行指定技能
 * @param[in] name 技能name 
 * @param[in] args param 
 * @param[out] out_buf output buffer 
 * @param[in] out_size buffer size
 * @return OKreturn 0，FAILreturn 非零
 * @note 对于external .md 技能，return 技能file body content 
 */
int axk_skill_execute(const char *name, const char *args, char *out_buf, size_t out_size);

/**
 * @brief from filesystemload /spiffs/skills/ directory 下技能file
 * @return OKload技能count ，FAILreturn 负数
 * @note support  .md format ，parse  frontmatter  name/trigger/description
 */
int axk_skill_load_from_fs(void);

/**
 * @brief get 指定external skillssystem提示词（body part ）
 * @param[in] name 技能name 
 * @param[out] buf output buffer 
 * @param[in] buf_size buffer size
 * @return OKreturn 0，not 找 to  or 非external skillsreturn -1
 * @note 供 Agent Loop 注入system提示词use 
 */
int axk_skill_get_prompt(const char *name, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_SKILL_LOADER_H */
