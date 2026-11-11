/*
 * Copyright 2026 Ngo Quoc Dat
 * SPDX-License-Identifier: MIT
 */

#include <check.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <virglrenderer.h>
#include "virgl_hw.h"
#include "virgl_protocol.h"
#include "pipe/p_defines.h"
#include "util/u_memory.h"
#include "testvirgl.h"
#include "testvirgl_encode.h"

static const char *fs_text =
   "FRAG\n"
   "DCL OUT[0], COLOR\n"
   "IMM[0] FLT32 { 1.0, 1.0, 1.0, 1.0 }\n"
   "  0: MOV OUT[0], IMM[0]\n"
   "  1: END\n";

static void flush_context(struct virgl_context *ctx)
{
   virgl_renderer_submit_cmd(ctx->cbuf->buf, ctx->ctx_id, ctx->cbuf->cdw);
   ctx->cbuf->cdw = 0;
}

static void open_context(struct virgl_context *ctx, int ctx_id)
{
   int ret = virgl_renderer_context_create(ctx_id, strlen("limits"), "limits");
   ck_assert_int_eq(ret, 0);
   ctx->ctx_id = ctx_id;
   ctx->flush = flush_context;
   ctx->cbuf = CALLOC_STRUCT(virgl_cmd_buf);
   ck_assert_ptr_nonnull(ctx->cbuf);
   ctx->cbuf->buf = CALLOC(1, VIRGL_MAX_CMDBUF_DWORDS * 4);
   ck_assert_ptr_nonnull(ctx->cbuf->buf);
}

static void close_context(struct virgl_context *ctx)
{
   virgl_renderer_context_destroy(ctx->ctx_id);
   FREE(ctx->cbuf->buf);
   FREE(ctx->cbuf);
}

static void set_limits(const struct virgl_renderer_limits *limits)
{
   struct virgl_renderer_limits versioned = *limits;
   versioned.version = VIRGL_RENDERER_LIMITS_VERSION;
   ck_assert_int_eq(virgl_renderer_set_limits(&versioned), 0);
}

static int create_blend(struct virgl_context *ctx, uint32_t handle)
{
   struct pipe_blend_state blend;

   memset(&blend, 0, sizeof(blend));
   blend.rt[0].colormask = PIPE_MASK_RGBA;
   virgl_encode_blend_state(ctx, handle, &blend);
   return testvirgl_ctx_send_cmdbuf(ctx);
}

static int delete_object(struct virgl_context *ctx, uint32_t handle, uint32_t type)
{
   virgl_encode_delete_object(ctx, handle, type);
   return testvirgl_ctx_send_cmdbuf(ctx);
}

static int create_sub_ctx(struct virgl_context *ctx, uint32_t sub_ctx_id)
{
   virgl_encoder_create_sub_ctx(ctx, sub_ctx_id);
   return testvirgl_ctx_send_cmdbuf(ctx);
}

/* A fragment shader in one packet that declares `length` bytes of text and
 * `num_tokens` tokens; past what it carries, the rest would follow. */
static int create_shader(struct virgl_context *ctx, uint32_t handle,
                         uint32_t length, uint32_t num_tokens)
{
   uint32_t size = strlen(fs_text) + 1;

   virgl_encoder_write_dword(ctx->cbuf, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SHADER,
                                                   VIRGL_OBJ_SHADER_HDR_SIZE(0) + (size + 3) / 4));
   virgl_encoder_write_dword(ctx->cbuf, handle);
   virgl_encoder_write_dword(ctx->cbuf, PIPE_SHADER_FRAGMENT);
   virgl_encoder_write_dword(ctx->cbuf, length);
   virgl_encoder_write_dword(ctx->cbuf, num_tokens);
   virgl_encoder_write_dword(ctx->cbuf, 0);
   virgl_encoder_write_block(ctx->cbuf, (const uint8_t *)fs_text, size);
   return testvirgl_ctx_send_cmdbuf(ctx);
}

/* Past its limit a context fails, as on a malformed command; others go on. */
START_TEST(virgl_test_limits_objects)
{
   struct virgl_context ctx, other;
   int ret;

   ret = testvirgl_init_ctx_cmdbuf(&ctx, context_flags);
   ck_assert_int_eq(ret, 0);
   set_limits(&(struct virgl_renderer_limits){ .context_objects = 3 });

   for (uint32_t handle = 1; handle <= 3; handle++)
      ck_assert_int_eq(create_blend(&ctx, handle), 0);
   /* a handle in use is replaced */
   ck_assert_int_eq(create_blend(&ctx, 2), 0);
   ck_assert_int_eq(create_blend(&ctx, 4), ENOMEM);

   open_context(&other, 2);
   for (uint32_t handle = 1; handle <= 3; handle++)
      ck_assert_int_eq(create_blend(&other, handle), 0);
   ck_assert_int_eq(delete_object(&ctx, 1, VIRGL_OBJECT_BLEND), EINVAL);
   close_context(&other);

   testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

START_TEST(virgl_test_limits_objects_freed)
{
   struct virgl_context ctx;
   int ret;

   ret = testvirgl_init_ctx_cmdbuf(&ctx, context_flags);
   ck_assert_int_eq(ret, 0);
   set_limits(&(struct virgl_renderer_limits){ .context_objects = 2 });

   ck_assert_int_eq(create_blend(&ctx, 1), 0);
   ck_assert_int_eq(create_blend(&ctx, 2), 0);
   ck_assert_int_eq(delete_object(&ctx, 1, VIRGL_OBJECT_BLEND), 0);
   ck_assert_int_eq(create_blend(&ctx, 3), 0);

   /* a sub-context's objects go with it */
   ck_assert_int_eq(delete_object(&ctx, 3, VIRGL_OBJECT_BLEND), 0);
   ck_assert_int_eq(create_sub_ctx(&ctx, 1), 0);
   ck_assert_int_eq(create_blend(&ctx, 1), 0);
   virgl_encoder_destroy_sub_ctx(&ctx, 1);
   ck_assert_int_eq(testvirgl_ctx_send_cmdbuf(&ctx), 0);
   ck_assert_int_eq(create_blend(&ctx, 4), 0);

   testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

START_TEST(virgl_test_limits_sub_contexts)
{
   struct virgl_context ctx;
   int ret;

   ret = testvirgl_init_ctx_cmdbuf(&ctx, context_flags);
   ck_assert_int_eq(ret, 0);
   /* sub-context 0 is one of them */
   set_limits(&(struct virgl_renderer_limits){ .context_sub_contexts = 2 });

   ck_assert_int_eq(create_sub_ctx(&ctx, 1), 0);
   ck_assert_int_eq(create_sub_ctx(&ctx, 1), 0);
   virgl_encoder_destroy_sub_ctx(&ctx, 1);
   ck_assert_int_eq(testvirgl_ctx_send_cmdbuf(&ctx), 0);
   ck_assert_int_eq(create_sub_ctx(&ctx, 2), 0);
   ck_assert_int_eq(create_sub_ctx(&ctx, 3), ENOMEM);

   testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

/* The total limits count all contexts, and a context that goes lets go of
 * its share. */
START_TEST(virgl_test_limits_total)
{
   struct virgl_context ctx, other;
   int ret;

   ret = testvirgl_init_ctx_cmdbuf(&ctx, context_flags);
   ck_assert_int_eq(ret, 0);
   set_limits(&(struct virgl_renderer_limits){ .total_sub_contexts = 3, .total_objects = 4 });

   open_context(&other, 2);
   ck_assert_int_eq(create_sub_ctx(&other, 1), 0);
   ck_assert_int_eq(virgl_renderer_context_create(3, strlen("limits"), "limits"), ENOMEM);

   for (uint32_t handle = 1; handle <= 3; handle++)
      ck_assert_int_eq(create_blend(&ctx, handle), 0);
   ck_assert_int_eq(create_blend(&other, 1), 0);
   ck_assert_int_eq(create_blend(&other, 2), ENOMEM);
   ck_assert_int_eq(delete_object(&ctx, 3, VIRGL_OBJECT_BLEND), 0);
   close_context(&other);

   ck_assert_int_eq(create_blend(&ctx, 3), 0);
   ck_assert_int_eq(create_blend(&ctx, 4), 0);
   open_context(&other, 3);
   close_context(&other);

   testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

/* A shader holds its text, and while it is parsed its tokens too: 300
 * tokens, 1240 bytes with the parser's ten spare ones. */
START_TEST(virgl_test_limits_shader_text)
{
   const uint32_t text = (strlen(fs_text) + 1 + 3) & ~3u;
   const uint32_t parse = (300 + 10) * 4;
   struct virgl_context ctx;
   int ret;

   ret = testvirgl_init_ctx_cmdbuf(&ctx, context_flags);
   ck_assert_int_eq(ret, 0);
   set_limits(&(struct virgl_renderer_limits){ .context_shader_bytes = 2 * text + parse - 4 });

   ck_assert_int_eq(create_shader(&ctx, 1, text, 300), 0);
   ck_assert_int_eq(delete_object(&ctx, 1, VIRGL_OBJECT_SHADER), 0);
   ck_assert_int_eq(create_shader(&ctx, 2, text, 300), 0);
   ck_assert_int_eq(create_shader(&ctx, 3, text, 300), ENOMEM);

   testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

/* A shader that is still arriving holds its text twice: in its object and in
 * the buffer that collects it. */
START_TEST(virgl_test_limits_shader_arriving)
{
   struct virgl_context ctx, other;
   int ret;

   ret = testvirgl_init_ctx_cmdbuf(&ctx, context_flags);
   ck_assert_int_eq(ret, 0);

   set_limits(&(struct virgl_renderer_limits){ .context_shader_bytes = 8192 });
   ck_assert_int_eq(create_shader(&ctx, 1, 4096, 300), 0);
   open_context(&other, 2);
   ck_assert_int_eq(create_shader(&other, 1, 1 << 20, 300), ENOMEM);
   close_context(&other);

   set_limits(&(struct virgl_renderer_limits){ .context_shader_bytes = 6000 });
   open_context(&other, 3);
   ck_assert_int_eq(create_shader(&other, 1, 4096, 300), ENOMEM);
   close_context(&other);

   testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

START_TEST(virgl_test_limits_shader_tokens)
{
   struct virgl_context ctx;
   int ret;

   ret = testvirgl_init_ctx_cmdbuf(&ctx, context_flags);
   ck_assert_int_eq(ret, 0);
   set_limits(&(struct virgl_renderer_limits){ .total_shader_bytes = 1 << 16 });

   ck_assert_int_eq(create_shader(&ctx, 1, strlen(fs_text) + 1, 1 << 20), ENOMEM);

   testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

/* One shader declares no more text and tokens than their limits, whatever
 * its context still holds. */
START_TEST(virgl_test_limits_shader_size)
{
   const uint32_t text = (strlen(fs_text) + 1 + 3) & ~3u;
   struct virgl_context ctx, other;
   int ret;

   ret = testvirgl_init_ctx_cmdbuf(&ctx, context_flags);
   ck_assert_int_eq(ret, 0);
   set_limits(&(struct virgl_renderer_limits){ .shader_text_bytes = 4096, .shader_tokens = 300 });

   ck_assert_int_eq(create_shader(&ctx, 1, text, 300), 0);
   open_context(&other, 2);
   ck_assert_int_eq(create_shader(&other, 1, 4096, 300), 0);
   close_context(&other);

   /* the declared length rounds up to whole words */
   open_context(&other, 3);
   ck_assert_int_eq(create_shader(&other, 1, 4097, 300), ENOMEM);
   close_context(&other);

   open_context(&other, 4);
   ck_assert_int_eq(create_shader(&other, 1, text, 301), ENOMEM);
   close_context(&other);

   ck_assert_int_eq(delete_object(&ctx, 1, VIRGL_OBJECT_SHADER), 0);
   testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

static bool has_streamout(void)
{
   uint32_t max_ver, max_size;
   struct virgl_caps_v1 *caps;
   bool has;

   /* capability set 1, VIRGL */
   virgl_renderer_get_cap_set(1, &max_ver, &max_size);
   caps = calloc(1, max_size);
   virgl_renderer_fill_caps(1, max_ver, caps);
   has = caps->max_streamout_buffers > 0;
   free(caps);
   return has;
}

/* Each new set of streamout targets gets a transform feedback object, an
 * object until a target in it goes. */
START_TEST(virgl_test_limits_streamout)
{
   struct virgl_context ctx;
   struct virgl_resource buffer;
   struct virgl_so_target first = { .handle = 1 }, second = { .handle = 2 };
   struct pipe_stream_output_target *targets[2];
   int ret;

   ret = testvirgl_init_ctx_cmdbuf(&ctx, context_flags);
   ck_assert_int_eq(ret, 0);
   if (!has_streamout()) {
      testvirgl_fini_ctx_cmdbuf(&ctx);
      return;
   }
   set_limits(&(struct virgl_renderer_limits){ .context_objects = 3 });

   ret = testvirgl_create_backed_simple_buffer(&buffer, 1, 256, PIPE_BIND_STREAM_OUTPUT);
   ck_assert_int_eq(ret, 0);
   virgl_renderer_ctx_attach_resource(ctx.ctx_id, buffer.handle);
   virgl_encoder_create_so_target(&ctx, first.handle, &buffer, 0, 128);
   virgl_encoder_create_so_target(&ctx, second.handle, &buffer, 128, 128);
   ck_assert_int_eq(testvirgl_ctx_send_cmdbuf(&ctx), 0);

   targets[0] = (struct pipe_stream_output_target *)&first;
   virgl_encoder_set_so_targets(&ctx, 1, targets, 0);
   virgl_encoder_set_so_targets(&ctx, 1, targets, 0);
   ck_assert_int_eq(testvirgl_ctx_send_cmdbuf(&ctx), 0);
   ck_assert_int_eq(delete_object(&ctx, first.handle, VIRGL_OBJECT_STREAMOUT_TARGET), 0);

   targets[0] = (struct pipe_stream_output_target *)&second;
   virgl_encoder_set_so_targets(&ctx, 1, targets, 0);
   ck_assert_int_eq(testvirgl_ctx_send_cmdbuf(&ctx), 0);
   ck_assert_int_eq(create_blend(&ctx, 3), 0);
   targets[1] = (struct pipe_stream_output_target *)&second;
   virgl_encoder_set_so_targets(&ctx, 2, targets, 0);
   ck_assert_int_eq(testvirgl_ctx_send_cmdbuf(&ctx), ENOMEM);

   virgl_renderer_ctx_detach_resource(ctx.ctx_id, buffer.handle);
   testvirgl_destroy_backed_res(&buffer);
   testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

START_TEST(virgl_test_limits_version)
{
   struct virgl_renderer_limits limits = { .version = VIRGL_RENDERER_LIMITS_VERSION + 1 };

   testvirgl_init_single_ctx_nr();
   ck_assert_int_eq(virgl_renderer_set_limits(&limits), EINVAL);
   ck_assert_int_eq(virgl_renderer_set_limits(NULL), EINVAL);
   testvirgl_fini_single_ctx();
}
END_TEST

static Suite *virgl_init_suite(void)
{
   Suite *s;
   TCase *tc_core;

   s = suite_create("virgl_limits");
   tc_core = tcase_create("limits");
   tcase_add_test(tc_core, virgl_test_limits_objects);
   tcase_add_test(tc_core, virgl_test_limits_objects_freed);
   tcase_add_test(tc_core, virgl_test_limits_sub_contexts);
   tcase_add_test(tc_core, virgl_test_limits_total);
   tcase_add_test(tc_core, virgl_test_limits_shader_text);
   tcase_add_test(tc_core, virgl_test_limits_shader_arriving);
   tcase_add_test(tc_core, virgl_test_limits_shader_tokens);
   tcase_add_test(tc_core, virgl_test_limits_shader_size);
   tcase_add_test(tc_core, virgl_test_limits_streamout);
   tcase_add_test(tc_core, virgl_test_limits_version);
   suite_add_tcase(s, tc_core);
   return s;
}

int main(void)
{
   Suite *s;
   SRunner *sr;
   int number_failed;

   if (getenv("VRENDTEST_USE_EGL_SURFACELESS"))
      context_flags |= VIRGL_RENDERER_USE_SURFACELESS;
   if (getenv("VRENDTEST_USE_EGL_GLES"))
      context_flags |= VIRGL_RENDERER_USE_GLES;

   s = virgl_init_suite();
   sr = srunner_create(s);

   srunner_run_all(sr, CK_NORMAL);
   number_failed = srunner_ntests_failed(sr);
   srunner_free(sr);

   return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
