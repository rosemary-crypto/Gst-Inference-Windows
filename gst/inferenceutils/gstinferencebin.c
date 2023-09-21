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

/**
 * SECTION:element-gstinferencebin
 *
 * Helper element that simplifies inference pipelines by creating a
 * bin with the required elements in the typical inference
 * configuration.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 v4l2src device=$CAMERA ! inferencebin arch=tinyyolov2 \
 * model-location=$MODEL_LOCATION backend=tensorflow input-layer=$INPUT_LAYER \
 * output-layer=$OUTPUT_LAYER labels="`cat labels.txt`" arch::iou-threshold=0.3 ! \
 * videoconvert ! ximagesink sync=false
 * ]|
 * Detects object in a camera stream
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstinferencebin.h"

/* generic templates */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_inference_bin_debug_category);
#define GST_CAT_DEFAULT gst_inference_bin_debug_category

static void gst_inference_bin_finalize (GObject * object);
static void gst_inference_bin_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_inference_bin_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_inference_bin_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_inference_bin_start (GstInferenceBin * self);
static gboolean gst_inference_bin_stop (GstInferenceBin * self);

enum
{
  PROP_0,
  PROP_ARCH,
  PROP_BACKEND,
  PROP_MODEL_LOCATION,
  PROP_INPUT_LAYER,
  PROP_OUTPUT_LAYER,
  PROP_LABELS,
  PROP_CROP,
  PROP_OVERLAY,
  PROP_FILTER,
  PROP_SCALER,
  PROP_CONVERTER,
};

#define PROP_ARCH_DEFAULT "tinyyolov2"
#define PROP_BACKEND_DEFAULT "tensorflow"
#define PROP_MODEL_LOCATION_DEFAULT NULL
#define PROP_INPUT_LAYER_DEFAULT NULL
#define PROP_OUTPUT_LAYER_DEFAULT NULL
#define PROP_LABELS_DEFAULT NULL
#define PROP_CROP_DEFAULT FALSE
#define PROP_OVERLAY_DEFAULT TRUE
#define PROP_FILTER_MIN -1
#define PROP_FILTER_MAX G_MAXINT32
#define PROP_FILTER_DEFAULT PROP_FILTER_MIN
#define PROP_SCALER_DEFAULT "videoscale"
#define PROP_CONVERTER_DEFAULT "videoconvert"

struct _GstInferenceBin
{
  GstBin parent;

  gchar *arch;
  gchar *backend;
  gchar *model_location;
  gchar *input_layer;
  gchar *output_layer;
  gchar *labels;
  gboolean crop;
  gboolean overlay;
  guint filter;
  gchar *scaler;
  gchar *converter;

  GstPad *sinkpad;
  GstPad *srcpad;
};

struct _GstInferenceBinClass
{
  GstBinClass parent;
};

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstInferenceBin, gst_inference_bin,
    GST_TYPE_BIN,
    GST_DEBUG_CATEGORY_INIT (gst_inference_bin_debug_category, "inferencebin",
        0, "debug category for inferencebin element"));

static void
gst_inference_bin_class_init (GstInferenceBinClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "inferencebin", "Filter",
      "A bin with the inference element in their typical configuration",
      "Michael Gruner <michael.gruner@ridgerun.com>");

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_inference_bin_change_state);

  object_class->finalize = gst_inference_bin_finalize;
  object_class->set_property = gst_inference_bin_set_property;
  object_class->get_property = gst_inference_bin_get_property;

  g_object_class_install_property (object_class, PROP_ARCH,
      g_param_spec_string ("arch", "Architecture",
          "The factory name of the network architecture to use for inference",
          PROP_ARCH_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_BACKEND,
      g_param_spec_string ("backend", "Backend",
          "The backend to use as the inference engine",
          PROP_BACKEND_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_MODEL_LOCATION,
      g_param_spec_string ("model-location", "Model location",
          "The location of the model to use for the inference",
          PROP_MODEL_LOCATION_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_INPUT_LAYER,
      g_param_spec_string ("input-layer", "Model input",
          "The name of the input of the model",
          PROP_INPUT_LAYER_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_OUTPUT_LAYER,
      g_param_spec_string ("output-layer", "Model output",
          "The name of the output of the model",
          PROP_OUTPUT_LAYER_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_LABELS,
      g_param_spec_string ("labels", "Model labels",
          "The labels used to train the model",
          PROP_LABELS_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_CROP,
      g_param_spec_boolean ("crop", "Crop",
          "Whether or not to crop out objects in the current prediction",
          PROP_CROP_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_OVERLAY,
      g_param_spec_boolean ("overlay", "Overlay",
          "Whether or not to overlay predictions on the buffers",
          PROP_OVERLAY_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_FILTER,
      g_param_spec_int ("filter", "Inference Filter",
          "The filter to apply to the inference (-1 disables).",
          PROP_FILTER_MIN, PROP_FILTER_MAX, PROP_FILTER_DEFAULT,
          G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_SCALER,
      g_param_spec_string ("scaler", "Video Scaler",
          "Bin description to use as video scaler",
          PROP_SCALER_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_CONVERTER,
      g_param_spec_string ("converter", "Color Space Converter",
          "Bin description to use as color space converter",
          PROP_CONVERTER_DEFAULT, G_PARAM_READWRITE));
}

static void
gst_inference_bin_init (GstInferenceBin * self)
{
  self->arch = g_strdup (PROP_ARCH_DEFAULT);
  self->backend = g_strdup (PROP_BACKEND_DEFAULT);
  self->model_location = g_strdup (PROP_MODEL_LOCATION_DEFAULT);
  self->input_layer = g_strdup (PROP_INPUT_LAYER_DEFAULT);
  self->output_layer = g_strdup (PROP_OUTPUT_LAYER_DEFAULT);
  self->labels = g_strdup (PROP_LABELS_DEFAULT);
  self->crop = PROP_CROP_DEFAULT;
  self->overlay = PROP_OVERLAY_DEFAULT;
  self->filter = PROP_FILTER_DEFAULT;
  self->scaler = g_strdup (PROP_SCALER_DEFAULT);
  self->converter = g_strdup (PROP_CONVERTER_DEFAULT);

  self->sinkpad =
      gst_ghost_pad_new_no_target_from_template (NULL,
      gst_static_pad_template_get (&sink_template));

  gst_pad_set_active (self->sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad =
      gst_ghost_pad_new_no_target_from_template (NULL,
      gst_static_pad_template_get (&src_template));
  gst_pad_set_active (self->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

static void
gst_inference_bin_finalize (GObject * object)
{
  GstInferenceBin *self = GST_INFERENCE_BIN (object);

  g_free (self->arch);
  self->arch = NULL;

  g_free (self->backend);
  self->backend = NULL;

  g_free (self->model_location);
  self->model_location = NULL;

  g_free (self->input_layer);
  self->input_layer = NULL;

  g_free (self->output_layer);
  self->output_layer = NULL;

  g_free (self->labels);
  self->labels = NULL;

  g_free (self->scaler);
  self->scaler = NULL;

  g_free (self->converter);
  self->converter = NULL;

  G_OBJECT_CLASS (gst_inference_bin_parent_class)->finalize (object);
}

static void
gst_inference_bin_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInferenceBin *self = GST_INFERENCE_BIN (object);

  GST_LOG_OBJECT (self, "set_property");

  GST_OBJECT_LOCK (self);

  switch (property_id) {
    case PROP_ARCH:
      g_free (self->arch);
      self->arch = g_value_dup_string (value);
      break;
    case PROP_BACKEND:
      g_free (self->backend);
      self->backend = g_value_dup_string (value);
      break;
    case PROP_MODEL_LOCATION:
      g_free (self->model_location);
      self->model_location = g_value_dup_string (value);
      break;
    case PROP_INPUT_LAYER:
      g_free (self->input_layer);
      self->input_layer = g_value_dup_string (value);
      break;
    case PROP_OUTPUT_LAYER:
      g_free (self->output_layer);
      self->output_layer = g_value_dup_string (value);
      break;
    case PROP_LABELS:
      g_free (self->labels);
      self->labels = g_value_dup_string (value);
      break;
    case PROP_CROP:
      self->crop = g_value_get_boolean (value);
      break;
    case PROP_OVERLAY:
      self->overlay = g_value_get_boolean (value);
      break;
    case PROP_FILTER:
      self->filter = g_value_get_int (value);
      break;
    case PROP_SCALER:
      g_free (self->scaler);
      self->scaler = g_value_dup_string (value);
      break;
    case PROP_CONVERTER:
      g_free (self->converter);
      self->converter = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_inference_bin_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstInferenceBin *self = GST_INFERENCE_BIN (object);

  GST_LOG_OBJECT (self, "get_property");

  GST_OBJECT_LOCK (self);

  switch (property_id) {
    case PROP_ARCH:
      g_value_set_string (value, self->arch);
      break;
    case PROP_BACKEND:
      g_value_set_string (value, self->backend);
      break;
    case PROP_MODEL_LOCATION:
      g_value_set_string (value, self->model_location);
      break;
    case PROP_INPUT_LAYER:
      g_value_set_string (value, self->input_layer);
      break;
    case PROP_OUTPUT_LAYER:
      g_value_set_string (value, self->output_layer);
      break;
    case PROP_LABELS:
      g_value_set_string (value, self->labels);
      break;
    case PROP_CROP:
      g_value_set_boolean (value, self->crop);
      break;
    case PROP_OVERLAY:
      g_value_set_boolean (value, self->overlay);
      break;
    case PROP_FILTER:
      g_value_set_int (value, self->filter);
      break;
    case PROP_SCALER:
      g_value_set_string (value, self->scaler);
      break;
    case PROP_CONVERTER:
      g_value_set_string (value, self->converter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static gchar *
gst_inference_bin_build_pipe (GstInferenceBin * self)
{
  GString *desc = NULL;
  const gchar *crop = NULL;
  const gchar *overlay = NULL;

  g_return_val_if_fail (self, FALSE);

  crop = self->crop ? "true" : "false";
  overlay = self->overlay ? "true" : "false";

  desc = g_string_new (NULL);

  g_string_append_printf (desc, "inferencefilter filter-class=%d name=filter "
      "! ", self->filter);
  g_string_append (desc, "inferencedebug name=debug_before ! ");
  g_string_append_printf (desc, "%s name=converter_before ! ", self->converter);
  g_string_append (desc, "tee name=tee ");
  g_string_append (desc,
      "tee. ! queue max-size-buffers=3 leaky=no name=queue_bypass ! "
      "arch.sink_bypass ");
  g_string_append_printf (desc, "tee. ! queue max-size-buffers=3 leaky=no "
      "name=queue_sink ! inferencecrop enable=%s name=crop ! ", crop);
  g_string_append_printf (desc, "%s name=scaler ! arch.sink_model ",
      self->scaler);
  g_string_append_printf (desc, "%s name=arch backend=%s model-location=%s ",
      self->arch, self->backend, self->model_location);

  if (self->labels) {
    g_string_append_printf (desc, "labels=\"%s\" ", self->labels);
  }

  if (self->input_layer) {
    g_string_append_printf (desc, "backend::input-layer=%s ",
        self->input_layer);
  }

  if (self->output_layer) {
    g_string_append_printf (desc, "backend::output-layer=%s ",
        self->output_layer);
  }

  g_string_append (desc, "arch.src_bypass ! queue name=queue_output ! "
      "inferencedebug name=debug_after ");

  g_string_append_printf (desc, "! inferenceoverlay enable=%s name=overlay ",
      overlay);

  return g_string_free (desc, FALSE);
}

static gboolean
gst_inference_bin_start (GstInferenceBin * self)
{
  gboolean ret = FALSE;
  GstElement *bin = NULL;
  GError *error = NULL;
  GstPad *sinkpad = NULL;
  GstPad *srcpad = NULL;
  gchar *pipe = NULL;

  g_return_val_if_fail (self, FALSE);

  GST_OBJECT_LOCK (self);
  pipe = gst_inference_bin_build_pipe (self);
  GST_OBJECT_UNLOCK (self);

  GST_INFO_OBJECT (self, "Attempting to build \"%s\"", pipe);

  bin =
      gst_parse_bin_from_description_full (pipe, TRUE, NULL,
      GST_PARSE_FLAG_FATAL_ERRORS, &error);

  g_free (pipe);

  if (!bin) {
    GST_ERROR_OBJECT (self, "Unable to create bin: %s", error->message);
    g_error_free (error);
    goto out;
  }

  if (!gst_bin_add (GST_BIN (self), bin)) {
    GST_ERROR_OBJECT (self, "Unable to add to bin");
    gst_object_unref (bin);
    goto out;
  }

  sinkpad = gst_element_get_static_pad (bin, "sink");
  srcpad = gst_element_get_static_pad (bin, "src");

  g_return_val_if_fail (sinkpad, FALSE);
  g_return_val_if_fail (srcpad, FALSE);

  gst_ghost_pad_set_target (GST_GHOST_PAD (self->sinkpad), sinkpad);
  gst_ghost_pad_set_target (GST_GHOST_PAD (self->srcpad), srcpad);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  GST_INFO_OBJECT (self, "Created bin successfully");
  ret = TRUE;

out:
  return ret;
}

static gboolean
gst_inference_bin_stop (GstInferenceBin * self)
{
  g_return_val_if_fail (self, FALSE);

  return TRUE;
}

static GstStateChangeReturn
gst_inference_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstInferenceBin *self = GST_INFERENCE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (FALSE == gst_inference_bin_start (self)) {
        GST_ERROR_OBJECT (self, "Failed to start");
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_inference_bin_parent_class)->change_state
      (element, transition);
  if (GST_STATE_CHANGE_FAILURE == ret) {
    GST_ERROR_OBJECT (self, "Parent failed to change state");
    goto out;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (FALSE == gst_inference_bin_stop (self)) {
        GST_ERROR_OBJECT (self, "Failed to stop");
        ret = GST_STATE_CHANGE_FAILURE;
        goto out;
      }
    default:
      break;
  }

out:
  return ret;
}
