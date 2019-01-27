//
// Created by divan on 24/01/19.
//

#include "timeline_ui.h"

TimelineUIState state = {};

String timeline_scaled_time_str(MemoryArena *arena, int64_t time) {
   if (time < 2000L)
      return str_print(arena, "%li ns", time);
   else if (time < 2000000L)
      return str_print(arena, "%.2f µs", time / 1000.0);
   else if (time < 2000000000L)
      return str_print(arena, "%.2f ms", time / 1000000.0);
   else
      return str_print(arena, "%.2f s", time / 1000000000.0);
}

String timeline_full_time_str(MemoryArena *arena, int64_t time) {
   if (!time) return str_copy(arena, "0");

   StringBuilder builder;
   strb_init(&builder, arena);

   int64_t secs = time / 1000000000;
   time -= secs * 1000000000;

   int64_t msecs = time / 1000000;
   time -= msecs * 1000000;

   int64_t usecs = time / 1000;
   time -= usecs * 1000;

   if (secs) strb_print(&builder, "%li s ", secs);
   if (msecs) strb_print(&builder, "%li ms ", msecs);
   if (usecs) strb_print(&builder, "%li µs ", usecs);
   if (time) strb_print(&builder, "%li ns ", time);

   return strb_done(&builder);
}

void timeline_chart_update(Context *ctx, cairo_t *cr, Timeline *timeline, i32rect area) {
   Colour chart_background = Colour{0.1, 0.1, 0.1};

   char buffer[4096];

   double event_height = 15.0;
   double thread_header_height = 20.0;

   cairo_font_extents_t font_extents;
   cairo_font_extents(cr, &font_extents);

   cairo_set_source_rgb(cr, chart_background);
   cairo_rectangle(cr, area.x, area.y, area.w, area.h);
   cairo_fill_preserve(cr);
   cairo_clip(cr);

   if (state.draw_time_width == 0) {
      state.draw_start_time = timeline->start_time;
      state.draw_time_width = timeline->end_time - timeline->start_time;
   }

   if (ctx->click_went_down) {
      state.click_draw_start_time = state.draw_start_time;
      state.click_draw_y = state.draw_y;
   }

   if (ctx->click) {
      state.draw_start_time = (int64_t) (state.click_draw_start_time +
                                         ((ctx->click_mouse_pos.x - ctx->mouse_pos.x) * state.draw_time_width) /
                                         area.w);
      state.draw_y = (int64_t) (state.click_draw_y + (ctx->click_mouse_pos.y - ctx->mouse_pos.y));
   }

   state.highlighted_event = nullptr;

   double mouse_time = state.draw_start_time + ((ctx->mouse_pos.x - area.x) * state.draw_time_width) / area.w;

   bool inside_area = inside(area, ctx->mouse_pos.x, ctx->mouse_pos.y);
   if (inside_area) {
      double zoom_factor = (ctx->mouse_delta_z > 0 ? (1.0 / 1.1) : 1.1);
      int zoom_iters = (ctx->mouse_delta_z > 0 ? ctx->mouse_delta_z : -ctx->mouse_delta_z);
      for (int i = 0; i < zoom_iters; i++) {
         state.draw_time_width *= zoom_factor;
      }
      state.draw_start_time = (int64_t) (mouse_time - ((ctx->mouse_pos.x - area.x) * state.draw_time_width) / area.w);
   }

   if (state.draw_start_time < timeline->start_time) {
      state.draw_start_time = timeline->start_time;
   }

   if (state.draw_time_width > (timeline->end_time - timeline->start_time)) {
      state.draw_time_width = timeline->end_time - timeline->start_time;
   }

   if (state.draw_y < 0) {
      state.draw_y = 0;
   }

   double draw_end_time = state.draw_start_time + state.draw_time_width;

   double width_scale = area.w / state.draw_time_width;

   double draw_y = area.y - state.draw_y + 25.0;

   for (int32_t thread_index = 0; thread_index < timeline->thread_count; thread_index++) {
      TimelineThread *thread = timeline->threads + thread_index;

      snprintf(buffer, array_size(buffer), "%u - %u - events: %li", thread->thread_id, thread->fiber_id,
               thread->event_count);

      cairo_text_extents_t text_extents;
      cairo_text_extents(cr, buffer, &text_extents);

      cairo_set_source_rgb(cr, Colour{0.2, 0.2, 0.2});
      cairo_move_to(cr, area.x, draw_y + thread_header_height - 0.5);
      cairo_line_to(cr, area.x + area.w, draw_y + thread_header_height - 0.5);
      cairo_stroke(cr);

      cairo_rectangle(cr, area.x, draw_y, text_extents.width + 8, thread_header_height);
      cairo_fill(cr);

      cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
      cairo_move_to(cr, area.x + 4, draw_y + (thread_header_height - font_extents.height) * 0.5 + font_extents.ascent);
      cairo_show_text(cr, buffer);

      draw_y += thread_header_height;

      for (int64_t event_index = 0; event_index < thread->event_count; event_index++) {
         TimelineEvent *event = thread->events + event_index;

         if (event->end_time < state.draw_start_time) {
            if (event->next_sibling_index) event_index = event->next_sibling_index - 1;
            continue;
         }

         if (event->start_time > draw_end_time) break;

         double x0 = (event->start_time - state.draw_start_time) * width_scale;
         double x1 = (event->end_time - state.draw_start_time) * width_scale;

         if (x0 < 0.0) x0 = 0.0;
         if (x1 > area.w) x1 = area.w;

         double w = x1 - x0;

         x0 += area.x;
         x1 += area.x;

         if (w >= 0.5) {
            double y0 = draw_y + event->depth * event_height;

            if (ctx->mouse_pos.x >= x0 && ctx->mouse_pos.x <= x1 && ctx->mouse_pos.y >= y0 &&
                ctx->mouse_pos.y < y0 + 15) {
               state.highlighted_event = event;
            }

            Colour background = {0.33, 0.67, 1.00};
            Colour text_colour = {0.9, 0.9, 0.9};

            if (event->method == state.active_method) {
               background = {1.0, 0.67, 0.33};
            }

            if (event == state.active_event) {
               background = {0.9, 0.9, 0.9};
               text_colour = {0.0, 0.0, 0.0};
            } else if (event == state.highlighted_event) {
               background *= 0.65;
            } else {
               background *= 0.50;
            }

            if (w >= 20.0) {
               cairo_set_source_rgb(cr, background.r * 0.6, background.g * 0.6, background.b * 0.6);
               cairo_rectangle(cr, x0, y0, w, event_height);
               cairo_fill(cr);

               cairo_set_source_rgb(cr, background.r, background.g, background.b);
               cairo_rectangle(cr, x0 + 0.5, y0 + 0.5, w - 1.0, event_height - 1.0);
               cairo_stroke(cr);

               double text_x = x0 + 2, text_w = w - 4;
               if (text_x < 2.0) {
                  text_x = 2.0;
                  text_w = x1 - text_x - 4;
               }

               cairo_save(cr);
               cairo_rectangle(cr, text_x, y0, text_w, 15);
               cairo_clip(cr);

               TimelineMethod *method = event->method;

               cairo_text_extents_t extents;
               cairo_text_extents(cr, method->name.data, &extents);

               cairo_set_source_rgb(cr, text_colour);
               cairo_move_to(cr, text_x, y0 + font_extents.ascent);
               cairo_show_text(cr, method->name.data);

               text_x += extents.x_advance + 4.0;

               String time_str = timeline_scaled_time_str(&ctx->temp, event->end_time - event->start_time);

               cairo_set_source_rgb(cr, lerp(0.25, text_colour, background));
               cairo_move_to(cr, text_x, y0 + font_extents.ascent);
               cairo_show_text(cr, time_str.data);

               cairo_restore(cr);
            } else {
               cairo_set_source_rgb(cr, lerp(0.7, background, chart_background));
               cairo_rectangle(cr, x0, y0, w, 15);
               cairo_fill(cr);
            }
         } else {
            if (event->next_sibling_index) {
               event_index = event->next_sibling_index - 1;
            }
         }
      }

      draw_y += event_height * thread->deepest_level;
      if (draw_y > ctx->height) break;
   }

   if (state.highlighted_event && ctx->click_went_up && (abs(ctx->click_mouse_pos.x - ctx->mouse_pos.x) <= 2)) {
      state.active_event = state.highlighted_event;
      ctx->dirty = true;
      if (ctx->double_click) {
         state.draw_start_time = state.active_event->start_time;
         state.draw_time_width = state.active_event->end_time - state.active_event->start_time;
      }

      tm_calculate_statistics(timeline, &state.selection_statistics,
                              state.active_event->start_time, state.active_event->end_time,
                              state.active_event->depth, state.method_sort_order_active);
   }

   // time axis
   {
      int64_t time_axis_height = 25;

      cairo_set_source_rgba(cr, 0.168, 0.168, 0.168, 0.6);
      cairo_rectangle(cr, area.x, area.y, area.w, time_axis_height);
      cairo_fill(cr);

      double interval_time = (state.draw_time_width / area.w) * 100.0;

      int64_t draw_interval = 1;

      while (interval_time > 1.0) {
         interval_time /= 10.0;
         draw_interval *= 10;
      }

      int64_t sub_draw_interval = draw_interval / 10;

      int64_t time = state.draw_start_time;
      time = (int64_t) (time / draw_interval) * draw_interval;

      while (time <= state.draw_start_time + state.draw_time_width) {
         double x = (time - state.draw_start_time) * width_scale;
         double rx = (int64_t) x + 0.5;

         cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
         cairo_move_to(cr, rx, area.y);
         cairo_line_to(cr, rx, area.y + 20.0);
         cairo_stroke(cr);

         String time_str = timeline_full_time_str(&ctx->temp, time);

         cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
         cairo_move_to(cr, rx + 5.0, area.y + 11.0 + (10.0 - font_extents.height) * 0.5 + font_extents.ascent);
         cairo_show_text(cr, time_str.data);

         cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
         cairo_move_to(cr, rx + 4.0, area.y + 10.0 + (10.0 - font_extents.height) * 0.5 + font_extents.ascent);
         cairo_show_text(cr, time_str.data);

         if (sub_draw_interval * width_scale > 40.0) {
            for (int i = 0; i < 9; i++) {
               time += sub_draw_interval;
               x = (time - state.draw_start_time) * width_scale;
               rx = (int64_t) x + 0.5;

               cairo_move_to(cr, rx, area.y);
               cairo_line_to(cr, rx, area.y + (i == 4 ? 15.0 : 8.0));
               cairo_stroke(cr);
            }

            time += sub_draw_interval;
         } else {
            time += draw_interval;
         }
      }
   }

   cairo_reset_clip(cr);
}

void timeline_methods_update(Context *ctx, cairo_t *cr, Timeline *timeline, i32rect area) {
   Colour methods_background = Colour{0.15, 0.15, 0.15};

   cairo_set_source_rgb(cr, methods_background);
   cairo_rectangle(cr, area.x, area.y, area.w, area.h);
   cairo_fill(cr);

   int64_t method_height = 30;

   TimelineStatistics *statistics = &state.selection_statistics;

   int64_t full_height = method_height * (statistics->method_count + 1);

   if (inside(area, ctx->mouse_pos)) {
      state.methods_scroll -= ctx->mouse_delta_z * method_height;
   }

   int64_t scroll_max = full_height - area.h;
   if (state.methods_scroll > scroll_max) state.methods_scroll = scroll_max;
   if (state.methods_scroll < 0) state.methods_scroll = 0;

   cairo_font_extents_t font_extents;
   cairo_font_extents(cr, &font_extents);

   int64_t y = area.y + method_height - state.methods_scroll;

   double column_pos[] = {0.0, area.w - 220.0, area.w - 160.0, area.w - 80.0};
   double column_width[] = {area.w - 220.0, 60.0, 80.0, 80.0};
   MethodSortOrder column_sort_order[] = {
         MethodSortOrder::NAME, MethodSortOrder::CALLS, MethodSortOrder::TOTAL_TIME, MethodSortOrder::SELF_TIME
   };

   // Methods
   irect table_inner_area = Rect(area.x, area.y + method_height, area.w, area.h - method_height);
   cairo_rectangle(cr, table_inner_area);
   cairo_clip(cr);

   if (!inside(table_inner_area, ctx->mouse_pos)) state.highlighted_method = nullptr;

   for (int32_t method_index = 0; method_index < statistics->method_count; method_index++) {
      auto method_statistics = statistics->method_statistics + method_index;
      auto method = method_statistics->method;

      if (inside(Rect(area.x, (int) y, area.w, (int) method_height), ctx->mouse_pos.x, ctx->mouse_pos.y)) {
         state.highlighted_method = method;
      }

      double self_time_fraction = (double) method_statistics->self_time / statistics->time_span;
      double total_time_fraction = (double) method_statistics->total_time / statistics->time_span;

      auto colour = Colour{0.33, 0.67, 1.00};
      auto shift = 0.0;

      if (method == state.active_method) {
         shift = 0.15;
         colour = Colour{0.6, 0.75, 0.90};
      } else if (method == state.highlighted_method) {
         shift = 0.1;
         colour = Colour{0.5, 0.7, 0.90};
      }

      cairo_set_source_rgb(cr, lerp(0.6 - shift, colour, methods_background));
      cairo_rectangle(cr, area.x, y, area.w * self_time_fraction, method_height);
      cairo_fill(cr);

      cairo_set_source_rgb(cr, lerp(0.8 - shift, colour, methods_background));
      cairo_rectangle(cr, area.x + area.w * self_time_fraction, y, area.w * (total_time_fraction - self_time_fraction),
                      method_height);
      cairo_fill(cr);

      cairo_set_source_rgb(cr, lerp(1.0 - shift, colour, methods_background));
      cairo_rectangle(cr, area.x + area.w * total_time_fraction, y, area.w * (1.0 - total_time_fraction),
                      method_height);
      cairo_fill(cr);

      auto text_first_y = y + (method_height * 0.5 - font_extents.height) * 0.5 + font_extents.ascent;
      auto text_second_y = text_first_y + method_height * 0.5;

      cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

      cairo_move_to(cr, area.x + column_pos[0] + 4, text_first_y);
      cairo_show_text(cr, method->name.data);

      cairo_text_extents_t text_extents;

      String calls_str = str_print(&ctx->temp, "%i", method_statistics->calls);
      cairo_text_extents(cr, calls_str.data, &text_extents);
      cairo_move_to(cr, area.x + column_pos[1] + column_width[1] - text_extents.width - 4, text_second_y);
      cairo_show_text(cr, calls_str.data);

      String total_str = timeline_scaled_time_str(&ctx->temp, method_statistics->total_time);
      cairo_text_extents(cr, total_str.data, &text_extents);
      cairo_move_to(cr, area.x + column_pos[2] + column_width[2] - text_extents.width - 4, text_second_y);
      cairo_show_text(cr, total_str.data);

      String self_str = timeline_scaled_time_str(&ctx->temp, method_statistics->self_time);
      cairo_text_extents(cr, self_str.data, &text_extents);
      cairo_move_to(cr, area.x + column_pos[3] + column_width[3] - text_extents.width - 4, text_second_y);
      cairo_show_text(cr, self_str.data);

      y += method_height;
      if (y > ctx->height) break;
   }

   cairo_reset_clip(cr);

   // Headers
   auto header_rect = Rect(area.x, area.y, area.w, method_height);

   cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
   cairo_move_to(cr, area.x, area.y + method_height - 0.5);
   cairo_line_to(cr, area.x + area.w, area.y + method_height - 0.5);
   cairo_stroke(cr);

   cairo_rectangle(cr, header_rect);
   cairo_clip(cr);

   if (inside(header_rect, ctx->mouse_pos)) {
      int column_index = -1;

      for (int i = 0; i < array_size(column_pos); i++) {
         if (ctx->mouse_pos.x - area.x < column_pos[i] + column_width[i]) {
            column_index = i;
            break;
         }
      }

      if (column_index >= 0) {
         state.method_sort_order_highlighted = column_sort_order[column_index];
      } else {
         state.method_sort_order_highlighted = MethodSortOrder::NONE;
      }

      if (ctx->click_went_up && state.method_sort_order_highlighted != MethodSortOrder::NONE) {
         MethodSortOrder old_sort_order = state.method_sort_order_active;
         state.method_sort_order_active = state.method_sort_order_highlighted;
         if (old_sort_order != state.method_sort_order_active) {
            tm_sort_statistics(&state.selection_statistics, state.method_sort_order_active);
            ctx->dirty = true;
         }
      }
   }

   auto text_header_y = area.y + (method_height - font_extents.height) * 0.5 + font_extents.ascent;
   cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

   cairo_text_extents_t text_extents;

   const char *methods_text = "Methods";
   cairo_move_to(cr, area.x + column_pos[0] + 4, text_header_y);
   cairo_show_text(cr, methods_text);

   const char *calls_text = "Calls";
   cairo_text_extents(cr, calls_text, &text_extents);
   cairo_move_to(cr, area.x + column_pos[1] + column_width[1] - text_extents.width - 4, text_header_y);
   cairo_show_text(cr, calls_text);

   const char *total_text = "Total";
   cairo_text_extents(cr, total_text, &text_extents);
   cairo_move_to(cr, area.x + column_pos[2] + column_width[2] - text_extents.width - 4, text_header_y);
   cairo_show_text(cr, total_text);

   const char *self_text = "Self";
   cairo_text_extents(cr, self_text, &text_extents);
   cairo_move_to(cr, area.x + column_pos[3] + column_width[3] - text_extents.width - 4, text_header_y);
   cairo_show_text(cr, self_text);

   cairo_reset_clip(cr);

   if (state.highlighted_method && ctx->click_went_up && (abs(ctx->click_mouse_pos.x - ctx->mouse_pos.x) <= 2)) {
      state.active_method = state.highlighted_method;
      ctx->dirty = true;
   }
}

void timeline_update(Context *ctx, cairo_t *cr, Timeline *timeline, i32rect area) {
   int method_panel_width = 400;
   int header_height = 20;

   cairo_set_font_size(cr, 10);

   cairo_font_extents_t font_extents;
   cairo_font_extents(cr, &font_extents);

   cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
   cairo_rectangle(cr, area.x, area.y, area.w, header_height);
   cairo_fill(cr);

   cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
   cairo_move_to(cr, area.x + 4, area.y + (header_height - font_extents.height) * 0.5 + font_extents.ascent);
   cairo_show_text(cr, timeline->name.data);

   timeline_chart_update(ctx, cr, timeline,
                         Rect(area.x, area.y + header_height, area.w - method_panel_width, area.h - header_height));

   int methods_x = area.x + area.w - method_panel_width;
   timeline_methods_update(ctx, cr, timeline,
                           Rect(methods_x, area.y + header_height, method_panel_width, area.h - header_height));

   cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
   cairo_move_to(cr, methods_x - 0.5, area.y + header_height);
   cairo_line_to(cr, methods_x - 0.5, area.y + area.h);
   cairo_stroke(cr);
}

void timeline_update_watch(Context *ctx, cairo_t *cr) {
   cairo_font_extents_t font_extents;
   cairo_font_extents(cr, &font_extents);

   cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
   cairo_rectangle(cr, 0, 0, state.watch_panel_width, ctx->height);
   cairo_fill_preserve(cr);
   cairo_clip(cr);

   double cur_y = 2.0 + font_extents.ascent;

   for (int64_t timeline_index = 0; timeline_index < state.timeline_count; timeline_index++) {
      Timeline *timeline = state.timelines + timeline_index;

      cairo_move_to(cr, 2.0, cur_y);
      cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
      cairo_show_text(cr, timeline->name.data);

      cur_y += font_extents.height + 2;

      cairo_move_to(cr, 2.0, cur_y);
      cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
      cairo_show_text(cr, timeline->filename.data);

      cur_y += font_extents.height + 4;
   }

   cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
   cairo_move_to(cr, state.watch_panel_width, 0.0);
   cairo_line_to(cr, state.watch_panel_width, ctx->height);
   cairo_stroke(cr);
   cairo_reset_clip(cr);
}

void update(Context *ctx, cairo_t *cr) {
   Timeline *timeline = &state.timeline;

   if (!state.thread) {
      uint64_t highest_events = 0;

      for (int thread_index = 0; thread_index < timeline->thread_count; thread_index++) {
         TimelineThread *thread = timeline->threads + thread_index;
         printf("Thread %i: [%u, %u] %li events\n", thread_index, thread->thread_id, thread->fiber_id,
                thread->event_count);
         if (thread->event_count >= highest_events) {
            state.thread = thread;
            highest_events = thread->event_count;
         }
      }
   }

   int timeline_view_x = 0;
   int timeline_view_width = ctx->width;

   if (str_nonblank(state.watch_path)) {
      state.watch_panel_open = true;
      state.watch_panel_width = 400;
      timeline_view_x = (int) state.watch_panel_width;
      timeline_view_width -= state.watch_panel_width;

      if (state.last_file_check <= 0.0 || state.last_file_check < ctx->proc_time - 3.0) {
         state.last_file_check = ctx->proc_time;

         printf("Scanning directory\n");
         file_list_free(&state.profile_file_list);
         file_list_init(&state.profile_file_list);
         file_read_directory(&state.profile_file_list, state.watch_path);

         raw_free(state.timelines);
         state.timeline_count = file_list_count(&state.profile_file_list);
         state.timelines = raw_alloc_array_zero(Timeline, state.timeline_count);

         int64_t timeline_index = 0;
         for (auto block = state.profile_file_list.first; block; block = block->next) {
            for (int i = 0; i < block->count; i++) {
               File *file = block->files + i;

               if (file->type == FILE_TYPE_FILE) {
                  String path = str_print(&ctx->temp, "%.*s/%.*s", str_prt(state.watch_path), str_prt(file->name));
                  if (tm_read_file_header(state.timelines + timeline_index, path.data)) {
                     timeline_index++;
                  }
               }
            }
         }

         state.timeline_count = timeline_index;
      }
   } else {
      state.watch_panel_open = false;
   }

   cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
   cairo_paint(cr);

   cairo_select_font_face(cr, "Source Code Pro", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
   cairo_set_font_size(cr, 10);

   cairo_font_extents_t font_extents;
   cairo_font_extents(cr, &font_extents);

   cairo_set_line_width(cr, 1.0);

   timeline_update(ctx, cr, timeline, Rect(timeline_view_x, 0, timeline_view_width, ctx->height));

   if (state.watch_panel_open) timeline_update_watch(ctx, cr);

   //
   // Tooltip
   //
   bool tooltip = state.highlighted_event || state.highlighted_method;

   if (tooltip) {

      cairo_text_extents_t text_extents;
      double sx = ctx->mouse_pos.x + 16;
      double sy = ctx->mouse_pos.y;
      double w = 0;

      if (sx + state.tooltip_w > ctx->width) {
         sx = ctx->mouse_pos.x - 16 - state.tooltip_w;
      }

      if (sx < 0) {
         sy += 24;
         sx = ctx->mouse_pos.x - state.tooltip_w * 0.5;
         if (sx < 0) sx = 0;
      }

      cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
      cairo_rectangle(cr, sx, sy, state.tooltip_w, state.tooltip_h);
      cairo_fill_preserve(cr);
      cairo_clip(cr);

      cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

      double x = sx;
      double y = sy + 4;

      TimelineMethod *method = nullptr;

      if (state.highlighted_event) {
         auto event = state.highlighted_event;
         method = event->method;
      } else if (state.highlighted_method) {
         method = state.highlighted_method;
      }

      String line = method->name;
      cairo_move_to(cr, x + 6, y + font_extents.ascent);
      cairo_show_text(cr, line.data);
      cairo_text_extents(cr, line.data, &text_extents);
      w = (w < text_extents.width) ? text_extents.width : w;
      y += font_extents.height;

      line = str_print(&ctx->temp, "%.*s:%i", str_prt(method->path), method->line_no);
      cairo_move_to(cr, x + 6, y + font_extents.ascent);
      cairo_show_text(cr, line.data);
      cairo_text_extents(cr, line.data, &text_extents);
      w = (w < text_extents.width) ? text_extents.width : w;
      y += font_extents.height;

      state.tooltip_w = w + 12;
      state.tooltip_h = y - sy + 8;

      cairo_reset_clip(cr);

      state.tooltip_w = w + 12;
      state.tooltip_h = y - sy + 8;

      cairo_reset_clip(cr);
   }
}