/*
 * Pinpoint: A small-ish presentation tool
 *
 * Copyright (C) 2010 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option0 any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Written by: Øyvind Kolås <pippin@linux.intel.com>
 *             Damien Lespiau <damien.lespiau@intel.com>
 *             Emmanuele Bassi <ebassi@linux.intel.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "pinpoint.h"

#ifdef USE_CLUTTER_GST
#include <clutter-gst/clutter-gst.h>
#endif

GList *pp_slides      = NULL; /* list of slide text */
GList *pp_slidep      = NULL; /* current slide */

typedef struct
{
  const char *name;
  int         value;
} EnumDescription;

static EnumDescription PPTextAlign_desc[] =
{
  { "left",   PP_TEXT_LEFT },
  { "center", PP_TEXT_CENTER },
  { "right",  PP_TEXT_RIGHT },
  { NULL,     0 }
};

#define PINPOINT_RENDERER(renderer) ((PinPointRenderer *) renderer)

/* pinpoint defaults */
static PinPointPoint pin_default_point = {
  .stage_color = "black",

  .bg = "NULL",
  .bg_type = PP_BG_NONE,
  .bg_scale = PP_BG_FIT,

  .text = NULL,
  .position = CLUTTER_GRAVITY_CENTER,
  .font = "Sans 60px",
  .text_color = "white",
  .text_align = PP_TEXT_LEFT,
  .use_markup = TRUE,

  .duration = 30,

  .speaker_notes = NULL,

  .shading_color = "black",
  .shading_opacity = 0.66,
  .transition = "fade",

  .command = NULL,

  .camera_framerate = 0,                    /* auto */
  .camera_resolution = {0, 0},              /* auto */

  .data = NULL,
};

static PinPointPoint default_point;

PinPointPoint *point_defaults = &default_point;

char     *pp_output_filename = NULL;
gboolean  pp_fullscreen      = FALSE;
gboolean  pp_maximized       = FALSE;
gboolean  pp_speakermode     = FALSE;
gboolean  pp_rehearse        = FALSE;
char     *pp_camera_device   = NULL;

static GOptionEntry entries[] =
{
    { "maximized", 'm', 0, G_OPTION_ARG_NONE, &pp_maximized,
    "Maximize without window decoration, instead\n"
"                                         of fullscreening, this is useful\n"
"                                         to enable window management when running\n"
"                                         [command=] spawned apps.", NULL},
    { "fullscreen", 'f', 0, G_OPTION_ARG_NONE, &pp_fullscreen,
    "Start in fullscreen mode", NULL},
    { "speakermode", 's', 0, G_OPTION_ARG_NONE, &pp_speakermode,
    "Show speakermode window", NULL},
    { "rehearse", 'r', 0, G_OPTION_ARG_NONE, &pp_rehearse,
    "Rehearse timings", NULL},
    { "output", 'o', 0, G_OPTION_ARG_STRING, &pp_output_filename,
      "Output presentation to FILE\n"
"                                         (formats supported: pdf)", "FILE" },
    { "camera", 'c', 0, G_OPTION_ARG_STRING, &pp_camera_device,
      "Device to use for [camera] background", "DEVICE" },
    { NULL }
};

PinPointRenderer *pp_clutter_renderer (void);
#ifdef HAVE_PDF
PinPointRenderer *pp_cairo_renderer   (void);
#endif
static char * pp_serialize (void);

void pp_rehearse_init (void)
{
  GList *iter;
  for (iter = pp_slides; iter; iter=iter->next)
    {
      PinPointPoint *point = iter->data;
      point->new_duration = 0.0;
    }
}


static char *pinfile = NULL;

static void pp_rehearse_save (void)
{
  GError *error = NULL;
  char *content = pp_serialize ();
  if (!g_file_set_contents (pinfile, content, -1, &error))
    {
      printf ("Failed to save to %s %s\n", pinfile, error->message);
    }
  else
    {
      printf ("saved to %s\n", pinfile);
    }
  g_free (content);
}


void pp_rehearse_done (void)
{
  GList *iter;
  for (iter = pp_slides; iter; iter=iter->next)
    {
      PinPointPoint *point = iter->data;
      point->duration = point->new_duration;
    }
  pp_rehearse_save ();
}

int
main (int    argc,
      char **argv)
{
  PinPointRenderer *renderer;
  GOptionContext   *context;
  GError *error = NULL;
  char   *text  = NULL;

  memcpy (&default_point, &pin_default_point, sizeof (default_point));
  renderer = pp_clutter_renderer ();

  context = g_option_context_new ("- Presentations made easy");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, clutter_get_option_group_without_init ());
  g_option_context_add_group (context, cogl_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("option parsing failed: %s\n", error->message);
      return EXIT_FAILURE;
    }

  pinfile = argv[1];

  if (!pinfile)
    {
      g_print ("usage: %s [options] <presentation>\n", argv[0]);
      text = g_strdup ("[no-markup][transition=sheet][red]\n"
                       "--\n"
                       "usage: pinpoint [options] <presentation.txt>\n");
    }
  else
    {
      if (!g_file_get_contents (pinfile, &text, NULL, NULL))
        {
          g_print ("failed to load presentation from %s\n", pinfile);
          return -1;
        }
    }

#ifdef USE_CLUTTER_GST
  clutter_gst_init (&argc, &argv);
#else
  clutter_init (&argc, &argv);
#endif
#ifdef USE_DAX
  dax_init (&argc, &argv);
#endif

  /* select the cairo renderer if we have requested pdf output */
  if (pp_output_filename && g_str_has_suffix (pp_output_filename, ".pdf"))
    {
#ifdef HAVE_PDF
      renderer = pp_cairo_renderer ();
      /* makes more sense to default to a white "stage" colour in PDFs*/
      default_point.stage_color = "white";
#else
      g_warning ("Pinpoint was built without PDF support");
      return EXIT_FAILURE;
#endif
    }

  if (!pinfile)
    pp_rehearse = FALSE;

  renderer->init (renderer, pinfile);
  pp_parse_slides (renderer, text);
  g_free (text);

  if (pp_rehearse)
    {
      pp_rehearse_init ();
      printf ("Running in rehearsal mode, press ctrl+C to abort without saving timings back to %s\n", pinfile);
    }
  renderer->run (renderer);
  renderer->finalize (renderer);
  if (renderer->source)
    g_free (renderer->source);
#if 0
  if (pp_rehearse)
    pp_rehearse_save ();
#endif

  g_list_free (pp_slides);

  return 0;
}

/*********************/


/*
 * Cross-renderer helpers
 */

void
pp_get_padding (float  stage_width,
                float  stage_height,
                float *padding)
{
  *padding = stage_width * 0.01;
}

void
pp_get_background_position_scale (PinPointPoint *point,
                                  float          stage_width,
                                  float          stage_height,
                                  float          bg_width,
                                  float          bg_height,
                                  float         *bg_x,
                                  float         *bg_y,
                                  float         *bg_scale_x,
                                  float         *bg_scale_y)
{
  float w_scale = stage_width / bg_width;
  float h_scale = stage_height / bg_height;

  switch (point->bg_scale)
    {
    case PP_BG_FILL:
      *bg_scale_x = *bg_scale_y = (w_scale > h_scale) ? w_scale : h_scale;
      break;
    case PP_BG_FIT:
      *bg_scale_x = *bg_scale_y = (w_scale < h_scale) ? w_scale : h_scale;
      break;
    case PP_BG_UNSCALED:
      *bg_scale_x = *bg_scale_y = (w_scale < h_scale) ? w_scale : h_scale;
      if (*bg_scale_x > 1.0)
        *bg_scale_x = *bg_scale_y = 1.0;
      break;
    case PP_BG_STRETCH:
      *bg_scale_x = w_scale;
      *bg_scale_y = h_scale;
      break;
    }
  *bg_x = (stage_width - bg_width * *bg_scale_x) / 2;
  *bg_y = (stage_height - bg_height * *bg_scale_y) / 2;
}

void
pp_get_text_position_scale (PinPointPoint *point,
                            float          stage_width,
                            float          stage_height,
                            float          text_width,
                            float          text_height,
                            float         *text_x,
                            float         *text_y,
                            float         *text_scale)
{
  float w, h;
  float x, y;
  float sx = 1.0;
  float sy = 1.0;
  float padding;

  pp_get_padding (stage_width, stage_height, &padding);

  w = text_width;
  h = text_height;

  sx = stage_width / w * 0.8;
  sy = stage_height / h * 0.8;

  if (sy < sx)
    sx = sy;
  if (sx > 1.0) /* avoid enlarging text */
    sx = 1.0;

  switch (point->position)
    {
      case CLUTTER_GRAVITY_EAST:
      case CLUTTER_GRAVITY_NORTH_EAST:
      case CLUTTER_GRAVITY_SOUTH_EAST:
        x = stage_width * 0.95 - w * sx;
        break;
      case CLUTTER_GRAVITY_WEST:
      case CLUTTER_GRAVITY_NORTH_WEST:
      case CLUTTER_GRAVITY_SOUTH_WEST:
        x = stage_width * 0.05;
        break;
      case CLUTTER_GRAVITY_CENTER:
      default:
        x = (stage_width - w * sx) / 2;
        break;
    }

  switch (point->position)
    {
      case CLUTTER_GRAVITY_SOUTH:
      case CLUTTER_GRAVITY_SOUTH_EAST:
      case CLUTTER_GRAVITY_SOUTH_WEST:
        y = stage_height * 0.95 - h * sx;
        break;
      case CLUTTER_GRAVITY_NORTH:
      case CLUTTER_GRAVITY_NORTH_EAST:
      case CLUTTER_GRAVITY_NORTH_WEST:
        y = stage_height * 0.05;
        break;
      default:
        y = (stage_height- h * sx) / 2;
        break;
    }

  *text_scale = sx;
  *text_x = x;
  *text_y = y;
}

void
pp_get_shading_position_size (float stage_width,
                              float stage_height,
                              float text_x,
                              float text_y,
                              float text_width,
                              float text_height,
                              float text_scale,
                              float *shading_x,
                              float *shading_y,
                              float *shading_width,
                              float *shading_height)
{
  float padding;

  pp_get_padding (stage_width, stage_height, &padding);

  *shading_x = text_x - padding;
  *shading_y = text_y - padding;
  *shading_width = text_width * text_scale + padding * 2;
  *shading_height = text_height * text_scale + padding * 2;
}

void     pp_parse_slides  (PinPointRenderer *renderer,
                           const char       *slide_src);

/*
 * Parsing
 */

static void
parse_resolution (PPResolution *r,
                  const gchar  *str)
{
  if (sscanf (str, "%dx%d", &r->width, &r->height) != 2)
    r->width = r->height = 0;
}

static void
parse_setting (PinPointPoint *point,
               const char    *setting)
{
/* C Preprocessor macros implemeting a mini language for interpreting
 * pinpoint key=value pairs
 */

#define START_PARSER if (0) {
#define DEFAULT      } else {
#define END_PARSER   }
#define IF_PREFIX(prefix) } else if (g_str_has_prefix (setting, prefix)) {
#define IF_EQUAL(string) } else if (g_str_equal (setting, string)) {
#define STRING  g_intern_string (strchr (setting, '=') + 1)
#define INT     atoi (strchr (setting, '=') + 1)
#define FLOAT   g_ascii_strtod (strchr (setting, '=') + 1, NULL)
#define RESOLUTION(r) parse_resolution (&r, strchr (setting, '=') + 1)
#define ENUM(r,t,s) \
  do { \
      int _i; \
      EnumDescription *_d = t##_desc; \
      r = _d[0].value; \
      for (_i = 0; _d[_i].name; _i++) \
        if (g_strcmp0 (_d[_i].name, s) == 0) \
          r = _d[_i].value; \
  } while (0)

  START_PARSER
  IF_PREFIX("stage-color=") point->stage_color = STRING;
  IF_PREFIX("font=")        point->font = STRING;
  IF_PREFIX("text-color=")  point->text_color = STRING;
  IF_PREFIX("text-align=")  ENUM(point->text_align, PPTextAlign, STRING);
  IF_PREFIX("shading-color=") point->shading_color = STRING;
  IF_PREFIX("shading-opacity=") point->shading_opacity = FLOAT;
  IF_PREFIX("duration=")   point->duration = FLOAT;
  IF_PREFIX("command=")    point->command = STRING;
  IF_PREFIX("transition=") point->transition = STRING;
  IF_PREFIX("camera-framerate=")  point->camera_framerate = INT;
  IF_PREFIX("camera-resolution=") RESOLUTION (point->camera_resolution);
  IF_EQUAL("fill")         point->bg_scale = PP_BG_FILL;
  IF_EQUAL("fit")          point->bg_scale = PP_BG_FIT;
  IF_EQUAL("stretch")      point->bg_scale = PP_BG_STRETCH;
  IF_EQUAL("unscaled")     point->bg_scale = PP_BG_UNSCALED;
  IF_EQUAL("center")       point->position = CLUTTER_GRAVITY_CENTER;
  IF_EQUAL("top")          point->position = CLUTTER_GRAVITY_NORTH;
  IF_EQUAL("bottom")       point->position = CLUTTER_GRAVITY_SOUTH;
  IF_EQUAL("left")         point->position = CLUTTER_GRAVITY_WEST;
  IF_EQUAL("right")        point->position = CLUTTER_GRAVITY_EAST;
  IF_EQUAL("top-left")     point->position = CLUTTER_GRAVITY_NORTH_WEST;
  IF_EQUAL("top-right")    point->position = CLUTTER_GRAVITY_NORTH_EAST;
  IF_EQUAL("bottom-left")  point->position = CLUTTER_GRAVITY_SOUTH_WEST;
  IF_EQUAL("bottom-right") point->position = CLUTTER_GRAVITY_SOUTH_EAST;
  IF_EQUAL("no-markup")    point->use_markup = FALSE;
  IF_EQUAL("markup")       point->use_markup = TRUE;
  DEFAULT                  point->bg = g_intern_string (setting);
  END_PARSER

/* undefine the overrides, returning us to regular C */
#undef START_PARSER
#undef END_PARSER
#undef DEFAULT
#undef IF_PREFIX
#undef IF_EQUAL
#undef FLOAT
#undef STRING
#undef INT
#undef ENUM
#undef RESOLUTION
}

static void
parse_config (PinPointPoint *point,
              const char    *config)
{
  GString    *str = g_string_new ("");
  const char *p;

  for (p = config; *p; p++)
    {
      if (*p != '[')
        continue;

      p++;
      g_string_truncate (str, 0);
      while (*p && *p != ']' && *p != '\n')
        {
          g_string_append_c (str, *p);
          p++;
        }

      if (*p == ']')
        parse_setting (point, str->str);
    }
  g_string_free (str, TRUE);
}

static void
pin_point_free (PinPointRenderer *renderer,
                PinPointPoint    *point)
{
  if (renderer->free_data)
    renderer->free_data (renderer, point->data);
  if (point->speaker_notes)
    {
      g_free (point->speaker_notes);
    }
  g_free (point);
}

static PinPointPoint *
pin_point_new (PinPointRenderer *renderer)
{
  PinPointPoint *point;

  point = g_new0 (PinPointPoint, 1);
  *point = default_point;

  if (renderer->allocate_data)
      point->data = renderer->allocate_data (renderer);

  return point;
}

static gboolean
pp_is_color (const char *string)
{
  ClutterColor color;
  return clutter_color_from_string (&color, string);
}

static gboolean
str_has_video_suffix (const char *string)
{
  char *video_extensions[] =
    {".avi", ".ogg", ".ogv", ".mpg",  ".flv", ".mpeg",
     ".mov", ".mp4", ".wmv", ".webm", ".mkv", NULL};
  char **ext;

  for (ext = video_extensions; *ext; ext ++)
    if (g_str_has_suffix (string, *ext))
      {
        return TRUE;
      }
  return FALSE;
}

static void serialize_slide_config (GString       *str,
                                    PinPointPoint *point,
                                    PinPointPoint *reference,
                                    const char    *separator)
{
#define STRING(v,n) \
  if (point->v != reference->v) \
    g_string_append_printf (str, "%s[" n "%s]", separator, point->v)
#define INT(v,n) \
  if (point->v != reference->v) \
    g_string_append_printf (str, "%s[" n "%d]", separator, point->v)
#define FLOAT(v,n) \
  if (point->v != reference->v) \
    g_string_append_printf (str, "%s[" n "%f]", separator, point->v)

  STRING(stage_color, "stage-color=");
  STRING(bg, "");

  if (point->bg_scale != reference->bg_scale)
    {
      g_string_append (str, separator);
      switch (point->bg_scale)
        {
          case PP_BG_FILL:     g_string_append (str, "[fill]");     break;
          case PP_BG_FIT:      g_string_append (str, "[fit]");      break;
          case PP_BG_STRETCH:  g_string_append (str, "[stretch]");  break;
          case PP_BG_UNSCALED: g_string_append (str, "[unscaled]"); break;
        }
    }

  if (point->text_align != reference->text_align)
    {
      g_string_append (str, separator);
      switch (point->text_align)
        {
          case PP_TEXT_LEFT:  g_string_append (str, "[text-align=left]");break;
          case PP_TEXT_CENTER:g_string_append (str, "[text-align=center]");break;
          case PP_TEXT_RIGHT: g_string_append (str, "[text-align=right]");break;
        }
    }

  if (point->position != reference->position)
    {
      g_string_append (str, separator);
      switch (point->position)
        {
          case CLUTTER_GRAVITY_NONE:
            break;
          case CLUTTER_GRAVITY_CENTER:
            g_string_append (str, "[center]");break;
          case CLUTTER_GRAVITY_NORTH:
            g_string_append (str, "[top]");break;
          case CLUTTER_GRAVITY_SOUTH:
            g_string_append (str, "[bottom]");break;
          case CLUTTER_GRAVITY_WEST:
            g_string_append (str, "[left]");break;
          case CLUTTER_GRAVITY_EAST:
            g_string_append (str, "[right]");break;
          case CLUTTER_GRAVITY_NORTH_WEST:
            g_string_append (str, "[top-left]");break;
          case CLUTTER_GRAVITY_NORTH_EAST:
            g_string_append (str, "[top-right]");break;
          case CLUTTER_GRAVITY_SOUTH_WEST:
            g_string_append (str, "[bottom-left]");break;
          case CLUTTER_GRAVITY_SOUTH_EAST:
            g_string_append (str, "[bottom-right]");break;
        }
    }

  STRING(font,"font=");
  STRING(text_color,"text-color=");
  STRING(shading_color,"shading-color=");
  FLOAT(shading_opacity, "shading-opacity=");

  STRING(transition,"transition=");
  STRING(command,"command=");
  if (point->duration != 0.0)
    FLOAT(duration, "duration="); /* XXX: probably needs special treatment */

  INT(camera_framerate, "camera-framerate=");
  if (point->camera_resolution.width != reference->camera_resolution.width &&
      point->camera_resolution.height != reference->camera_resolution.height)
    {
        g_string_append_printf (str, "[camera-resolution=%dx%d]",
                                point->camera_resolution.width,
                                point->camera_resolution.height);
    }

  if (point->use_markup != reference->use_markup)
    {
      g_string_append (str, separator);
      if (point->use_markup)
        g_string_append (str, "[markup]");
      else
        g_string_append (str, "[no-markup]");
    }

#undef FLOAT
#undef INT
#undef STRING
}


static void serialize_slide (GString *str,
                             PinPointPoint *point)
{
  g_string_append_c (str, '\n');
  g_string_append (str, "--");
  serialize_slide_config (str, point, &default_point, " ");
  g_string_append (str, "\n");

  g_string_append_printf (str, "%s\n", point->text);

  if (point->speaker_notes)
    {
      char *p;
      g_string_append_c (str, '#');
      for (p = point->speaker_notes; *p; p++)
        {
          if (*p == '\n')
            {
              g_string_append_c (str, '\n');
              if (*(p+1))
                g_string_append_c (str, '#');
            }
          else
            {
              g_string_append_c (str, *p);
            }
        }
    }
}

static char * pp_serialize (void)
{
  GString *str = g_string_new ("#!/usr/bin/env pinpoint\n");
  char *ret;
  GList *iter;

  serialize_slide_config (str, &default_point, &pin_default_point, "\n");

  for (iter = pp_slides; iter; iter = iter->next)
    {
      serialize_slide (str, iter->data);
    }
  ret = str->str;
  g_string_free (str, FALSE);
  return ret;
}

void
pp_parse_slides (PinPointRenderer *renderer,
                 const char       *slide_src)
{
  const char *p;
  int         slideno     = 0;
  gboolean    done        = FALSE;
  gboolean    startofline = TRUE;
  gboolean    gotconfig   = FALSE;
  GString    *slide_str   = g_string_new ("");
  GString    *setting_str = g_string_new ("");
  GString    *notes_str   = g_string_new ("");
  GList      *s;
  PinPointPoint *point, *next_point;

  if (renderer->source)
    {
      gboolean start_of_line = TRUE;
      int pos;
      int lineno=0;
      /* compute slide no that has changed */
      for (pos = 0, slideno = 0;
           slide_src[pos] &&
           renderer->source[pos] &&
           slide_src[pos]==renderer->source[pos]
           ; pos ++)
        {
          switch (slide_src[pos])
            {
              case '\n':
                start_of_line = TRUE;
                lineno++;
                break;
              case '-':
                if (start_of_line)
                  slideno++;
              default:
                start_of_line = FALSE;
            }
        }
      slideno-=1;
      g_free (renderer->source);
    }
  renderer->source = g_strdup (slide_src);

  for (s = pp_slides; s; s = s->next)
    pin_point_free (renderer, s->data);

  g_list_free (pp_slides);
  pp_slides = NULL;
  point = pin_point_new (renderer);

  /* parse the slides, constructing lists of slide/point objects
   */
  for (p = slide_src; *p; p++)
    {
      switch (*p)
        {
          case '\\': /* escape the next char */
            p++;
            startofline = FALSE;
            if (*p)
              g_string_append_c (slide_str, *p);
            break;
          case '\n':
            startofline = TRUE;
            g_string_append_c (slide_str, *p);
            break;
          case '-': /* slide seperator */
            close_last_slide:
            if (startofline)
              {
                startofline = FALSE;
                next_point = pin_point_new (renderer);

                g_string_assign (setting_str, "");
                while (*p && *p!='\n')  /* until newline */
                  {
                    g_string_append_c (setting_str, *p);
                    p++;
                  }
                parse_config (next_point, setting_str->str);

                if (!gotconfig)
                  {
                    parse_config (&default_point, slide_str->str);
                    /* copy the default point except the per-slide allocated
                     * data (void *) */
                    memcpy (point, &default_point,
                            sizeof (PinPointPoint) - sizeof (void *));
                    parse_config (point, setting_str->str);
                    gotconfig = TRUE;
                    g_string_assign (slide_str, "");
                    g_string_assign (setting_str, "");
                    g_string_assign (notes_str, "");
                  }
                else
                  {
                    if (point->bg && point->bg[0])
                      {
                        char *filename = g_strdup (point->bg);
                        int i = 0;

                        while (filename[i])
                          {
                            filename[i] = tolower(filename[i]);
                            i++;
                          }

                        if (strcmp (filename, "camera") == 0)
                          point->bg_type = PP_BG_CAMERA;
                        else if (str_has_video_suffix (filename))
                          point->bg_type = PP_BG_VIDEO;
                        else if (g_str_has_suffix (filename, ".svg"))
                          point->bg_type = PP_BG_SVG;
                        else if (pp_is_color (point->bg))
                          point->bg_type = PP_BG_COLOR;
                        else
                          point->bg_type = PP_BG_IMAGE;
                        g_free (filename);
                      }

                    {
                      char *str = slide_str->str;

                    /* trim newlines from start and end. ' ' can be used in the
                     * insane case that you actually want blank lines before or
                     * after the text of a slide */
                      while (*str == '\n') str++;
                      while ( slide_str->str[strlen(slide_str->str)-1]=='\n')
                        slide_str->str[strlen(slide_str->str)-1]='\0';

                      point->text = g_intern_string (str);
                    }
                    if (notes_str->str[0])
                      point->speaker_notes = g_strdup (notes_str->str);

                    renderer->make_point (renderer, point);

                    g_string_assign (slide_str, "");
                    g_string_assign (setting_str, "");
                    g_string_assign (notes_str, "");

                    pp_slides = g_list_append (pp_slides, point);
                    point = next_point;
                  }
              }
            else
              {
                g_string_append_c (slide_str, *p);
              }
            break;
        case '#': /* comment */
          if (startofline)
            {
              const char *end = p + 1;
              while (*end != '\n' && *end != '\0')
                {
                  g_string_append_c (notes_str, *end);
                  end++;
                }
              if (end)
                {
                  g_string_append_c (notes_str, '\n');
                  p = end;
                  break;
                }
            }
          /* flow through */
          default:
            startofline = FALSE;
            g_string_append_c (slide_str, *p);
            break;
        }
    }

  if (!done)
    {
      done = TRUE;
      goto close_last_slide;
    }

  g_string_free (slide_str, TRUE);
  g_string_free (setting_str, TRUE);
  g_string_free (notes_str, TRUE);

  if (g_list_nth (pp_slides, slideno))
    pp_slidep = g_list_nth (pp_slides, slideno);
  else
    pp_slidep = pp_slides;
}
