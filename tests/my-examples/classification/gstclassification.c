/*
 * GStreamer
 * Copyright (C) 2018-2020 RidgeRun <support@ridgerun.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "customlogic.h"
#include "gst/r2inference/gstinferencemeta.h"

#define GETTEXT_PACKAGE "GstInference"

typedef struct _GstClassification GstClassification;
struct _GstClassification
{
  GstElement *pipeline;
  GMainLoop *main_loop;
  GstElement *inference_element;
};

GstClassification *gst_classification_new (void);
void gst_classification_free (GstClassification * classification);
void gst_classification_create_pipeline (GstClassification * classification);
void gst_classification_start (GstClassification * classification);
void gst_classification_stop (GstClassification * classification);
static void gst_classification_process_inference (GstElement * element,
    GstInferenceMeta * model_meta, GstVideoFrame * model_frame,
    GstInferenceMeta * bypass_meta, GstVideoFrame * bypass_frame,
    gpointer user_data);
static gboolean gst_classification_exit_handler (gpointer user_data);
static gboolean gst_classification_handle_message (GstBus * bus,
    GstMessage * message, gpointer data);
gboolean verbose;
static const gchar *model_path = NULL;
static const gchar *file_path = NULL;
static const gchar *backend = NULL;

static GOptionEntry entries[] = {
  {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL},
  {"model", 'm', 0, G_OPTION_ARG_STRING, &model_path, "Model path", NULL},
  {"file", 'f', 0, G_OPTION_ARG_STRING, &file_path,
      "File path (or camera, if omitted)", NULL},
  {"backend", 'b', 0, G_OPTION_ARG_STRING, &backend,
      "Backend used for inference, example: tensorflow", NULL},
  {NULL}
};

int
main (int argc, char *argv[])
{
  GError *error = NULL;
  GOptionContext *context;
  GstClassification *classification;
  GMainLoop *main_loop;

  context = g_option_context_new (" - GstInference Classification Example");
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("option parsing failed: %s\n", error->message);
    g_clear_error (&error);
    g_option_context_free (context);
    exit (1);
  }
  g_option_context_free (context);

  if (verbose) {
    g_print ("Model Path: %s \n", model_path);
    g_print ("File path: %s \n", file_path ? file_path : "camera");
    g_print ("Backend: %s \n", backend);
  }

  if (!backend) {
    g_printerr ("Backend is required for inference (-b <backend>) \n");
    exit (1);
  }

  if (!model_path) {
    g_printerr ("Model path is required (-m <path>) \n");
    exit (1);
  }

  classification = gst_classification_new ();
  gst_classification_create_pipeline (classification);

  g_unix_signal_add (SIGINT, gst_classification_exit_handler, classification);
  classification->inference_element =
      gst_bin_get_by_name (GST_BIN (classification->pipeline), "net");

  g_signal_connect (classification->inference_element, "new-inference",
      G_CALLBACK (gst_classification_process_inference), classification);

  gst_classification_start (classification);

  main_loop = g_main_loop_new (NULL, TRUE);
  classification->main_loop = main_loop;

  g_main_loop_run (main_loop);

  gst_classification_stop (classification);
  gst_classification_free (classification);

  exit (0);
}

static gboolean
gst_classification_exit_handler (gpointer data)
{
  GstClassification *classification;

  g_return_val_if_fail (data, FALSE);

  classification = (GstClassification *) data;
  g_main_loop_quit (classification->main_loop);

  return TRUE;
}

GstClassification *
gst_classification_new (void)
{
  GstClassification *classification;

  classification = g_new0 (GstClassification, 1);

  return classification;
}

void
gst_classification_free (GstClassification * classification)
{

  g_return_if_fail (classification);

  if (classification->inference_element) {
    gst_object_unref (classification->inference_element);
    classification->inference_element = NULL;
  }

  if (classification->pipeline) {
    gst_element_set_state (classification->pipeline, GST_STATE_NULL);
    gst_object_unref (classification->pipeline);
    classification->pipeline = NULL;
  }
  if (classification->main_loop) {
    g_main_loop_unref (classification->main_loop);
    classification->main_loop = NULL;
  }

  g_free (classification);
}

static void
gst_classification_process_inference (GstElement * element,
    GstInferenceMeta * model_meta, GstVideoFrame * model_frame,
    GstInferenceMeta * bypass_meta, GstVideoFrame * bypass_frame,
    gpointer user_data)
{
  GstInferenceClassification *classification = NULL;
  GstInferencePrediction *prediction = NULL;

  g_return_if_fail (element);
  g_return_if_fail (model_meta);
  g_return_if_fail (model_frame);
  g_return_if_fail (bypass_meta);
  g_return_if_fail (bypass_frame);
  g_return_if_fail (user_data);

  prediction = bypass_meta->prediction;

  GST_INFERENCE_PREDICTION_LOCK (prediction);

  classification =
      (GstInferenceClassification *) prediction->classifications->data;

  handle_prediction (bypass_frame->data[0], bypass_frame->info.width,
      bypass_frame->info.height, bypass_frame->info.size,
      classification->probabilities, classification->num_classes);

  GST_INFERENCE_PREDICTION_UNLOCK (prediction);
}

void
gst_classification_create_pipeline (GstClassification * classification)
{
  GString *pipe_desc;
  GstElement *pipeline;
  GError *error = NULL;
  GstBus *bus;

  g_return_if_fail (classification);

  pipe_desc = g_string_new ("");

  g_string_append (pipe_desc, " inceptionv4 name=net backend=");
  g_string_append (pipe_desc, backend);
  g_string_append (pipe_desc, " model-location=");
  g_string_append (pipe_desc, model_path);
  if (!strcmp (backend, "tensorflow")) {
    g_string_append (pipe_desc, " backend::input-layer=input ");
    g_string_append (pipe_desc,
        " backend::output-layer=InceptionV4/Logits/Predictions ");
  }
  if (file_path) {
    g_string_append (pipe_desc, " filesrc location=");
    g_string_append (pipe_desc, file_path);
    g_string_append (pipe_desc, " ! decodebin ! ");
  } else {
    g_string_append (pipe_desc, " autovideosrc !  ");
  }
  g_string_append (pipe_desc, " tee name=t t. ! queue ! videoconvert ! ");
  g_string_append (pipe_desc, " videoscale ! net.sink_model t. ! ");
  g_string_append (pipe_desc, " queue ! videoconvert ! ");
  g_string_append (pipe_desc, " video/x-raw,format=RGB ! net.sink_bypass ");
  g_string_append (pipe_desc, " net.src_bypass ! inferenceoverlay ! ");
  g_string_append (pipe_desc, " videoconvert ! queue ! ");
  g_string_append (pipe_desc, " autovideosink sync=false");

  if (verbose)
    g_print ("pipeline: %s\n", pipe_desc->str);

  pipeline = GST_ELEMENT (gst_parse_launch (pipe_desc->str, &error));
  g_string_free (pipe_desc, TRUE);

  if (error) {
    g_print ("pipeline parsing error: %s\n", error->message);
    g_clear_error (&error);
    gst_object_unref (pipeline);
    return;
  }

  classification->pipeline = pipeline;

  gst_pipeline_set_auto_flush_bus (GST_PIPELINE (pipeline), FALSE);
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, gst_classification_handle_message, classification);
  gst_object_unref (bus);
}

void
gst_classification_start (GstClassification * classification)
{
  g_return_if_fail (classification);

  gst_element_set_state (classification->pipeline, GST_STATE_PLAYING);
}

void
gst_classification_stop (GstClassification * classification)
{
  g_return_if_fail (classification);

  gst_element_set_state (classification->pipeline, GST_STATE_NULL);
}

static void
gst_classification_handle_eos (GstClassification * classification)
{
  g_return_if_fail (classification);

  g_main_loop_quit (classification->main_loop);
}

static void
gst_classification_handle_error (GstClassification * classification,
    GError * error, const char *debug)
{
  g_return_if_fail (classification);
  g_return_if_fail (error);
  g_return_if_fail (debug);

  g_printerr ("error: %s\n", error->message);
  g_main_loop_quit (classification->main_loop);
}

static void
gst_classification_handle_warning (GstClassification * classification,
    GError * error, const char *debug)
{
  g_return_if_fail (classification);
  g_return_if_fail (error);
  g_return_if_fail (debug);

  g_printerr ("warning: %s\n", error->message);
}

static void
gst_classification_handle_info (GstClassification * classification,
    GError * error, const char *debug)
{
  g_return_if_fail (classification);
  g_return_if_fail (error);
  g_return_if_fail (debug);

  g_print ("info: %s\n", error->message);
}

static gboolean
gst_classification_handle_message (GstBus * bus, GstMessage * message,
    gpointer data)
{
  GstClassification *classification;

  g_return_val_if_fail (bus, FALSE);
  g_return_val_if_fail (message, FALSE);
  g_return_val_if_fail (data, FALSE);

  classification = (GstClassification *) data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      gst_classification_handle_eos (classification);
      break;
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug;

      gst_message_parse_error (message, &error, &debug);
      gst_classification_handle_error (classification, error, debug);
      g_clear_error (&error);
    }
      break;
    case GST_MESSAGE_WARNING:
    {
      GError *error = NULL;
      gchar *debug;

      gst_message_parse_warning (message, &error, &debug);
      gst_classification_handle_warning (classification, error, debug);
      g_clear_error (&error);
    }
      break;
    case GST_MESSAGE_INFO:
    {
      GError *error = NULL;
      gchar *debug;

      gst_message_parse_info (message, &error, &debug);
      gst_classification_handle_info (classification, error, debug);
      g_clear_error (&error);
    }
      break;
    default:
      if (verbose) {
        g_print ("message: %s\n", GST_MESSAGE_TYPE_NAME (message));
      }
      break;
  }

  return TRUE;
}
