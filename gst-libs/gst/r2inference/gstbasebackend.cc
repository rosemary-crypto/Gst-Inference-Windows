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

#include "gstbasebackend.h"
#include "gstbasebackendsubclass.h"

#include <r2i/r2i.h>

#include <cstring>
#include <memory>
#include <list>

GST_DEBUG_CATEGORY_STATIC (gst_base_backend_debug_category);
#define GST_CAT_DEFAULT gst_base_backend_debug_category

#define DOUBLE_PROPERTY_DEFAULT_VALUE 0.0

class InferenceProperty {
 private:

  GValue *avalue;
  GParamSpec *apspec;

 public:

  InferenceProperty() {
  }

  InferenceProperty(const GValue *value, GParamSpec *pspec) {
    avalue = (GValue *) g_malloc(sizeof(GValue));
    *avalue = G_VALUE_INIT;
    avalue = g_value_init (avalue, pspec->value_type);
    g_value_copy (value, avalue);
    g_param_spec_ref (pspec);
    apspec = pspec;
  }

  ~InferenceProperty() {
    g_param_spec_unref (apspec);
    g_free(avalue);
  }

  void apply_inference_property(GstBaseBackend *self,
                                std::shared_ptr<r2i::IParameters> params, r2i::RuntimeError &error) {
    switch (apspec->value_type) {
      case G_TYPE_STRING:
        GST_INFO_OBJECT (self, "Setting property: %s=%s\n", apspec->name,
                         g_value_get_string(avalue));
        error = params->Set(apspec->name, g_value_get_string(avalue));
        break;
      case G_TYPE_INT:
        GST_INFO_OBJECT (self, "Setting property: %s=%d\n", apspec->name,
                         g_value_get_int(avalue));
        error = params->Set(apspec->name, g_value_get_int(avalue));
        break;
      case G_TYPE_DOUBLE:
        GST_INFO_OBJECT (self, "Setting property: %s=%f\n", apspec->name,
                         g_value_get_double(avalue));
        error = params->Set(apspec->name, g_value_get_double(avalue));
        break;
      default:
        GST_WARNING_OBJECT (self, "Invalid property type");
        break;
    }
  }

  const gchar *get_name() {
    return apspec->name;
  }
};

typedef struct _GstBaseBackendPrivate GstBaseBackendPrivate;
struct _GstBaseBackendPrivate {
  r2i::FrameworkCode code;
  std::shared_ptr < r2i::IEngine > engine;
  std::shared_ptr < r2i::ILoader > loader;
  std::shared_ptr < r2i::IModel > model;
  std::shared_ptr < r2i::IParameters > params;
  std::unique_ptr < r2i::IFrameworkFactory > factory;
  GMutex backend_mutex;
  gboolean backend_started;
  std::shared_ptr < std::list<InferenceProperty *> > property_list;
  gboolean backend_created;

};

G_DEFINE_TYPE_WITH_CODE (GstBaseBackend, gst_base_backend, G_TYPE_OBJECT,
                         GST_DEBUG_CATEGORY_INIT (gst_base_backend_debug_category, "basebackend", 0,
                             "debug category for backend parameters"); G_ADD_PRIVATE (GstBaseBackend));

#define GST_BASE_BACKEND_PRIVATE(self) \
  (GstBaseBackendPrivate *)(gst_base_backend_get_instance_private (self))

static GParamSpec *gst_base_backend_param_to_spec (r2i::ParameterMeta *param);
static int gst_base_backend_param_flags (int flags);
static void gst_base_backend_finalize (GObject *obj);

#define GST_BASE_BACKEND_ERROR gst_base_backend_error_quark()

static void
gst_base_backend_class_init (GstBaseBackendClass *klass) {
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->set_property = gst_base_backend_set_property;
  oclass->get_property = gst_base_backend_get_property;
  oclass->finalize = gst_base_backend_finalize;

}

static void
gst_base_backend_init (GstBaseBackend *self) {
  GstBaseBackendPrivate *priv = GST_BASE_BACKEND_PRIVATE (self);
  g_mutex_init(&priv->backend_mutex);
  priv->backend_started = false;
  priv->backend_created = false;
  priv->property_list = std::make_shared<std::list<InferenceProperty *>>();
}

static void
gst_base_backend_finalize (GObject *obj) {
  GstBaseBackend *self = GST_BASE_BACKEND (obj);
  GstBaseBackendPrivate *priv = GST_BASE_BACKEND_PRIVATE (self);
  g_mutex_clear (&priv->backend_mutex);

  priv->engine = nullptr;
  priv->loader = nullptr;
  priv->model = nullptr;
  priv->params = nullptr;
  priv->factory = nullptr;
  priv-> property_list = nullptr;

  G_OBJECT_CLASS (gst_base_backend_parent_class)->finalize (obj);
}

void
gst_base_backend_install_properties (GstBaseBackendClass *klass,
                                r2i::FrameworkCode code) {
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  r2i::RuntimeError error;
  std::vector < r2i::ParameterMeta > params;
  gint nprop = 1;

  auto factory = r2i::IFrameworkFactory::MakeFactory (code, error);
  auto pfactory = factory->MakeParameters (error);
  if (pfactory)
      error = pfactory->List (params);

  for (auto &param : params) {
    GParamSpec *spec = gst_base_backend_param_to_spec (&param);
    g_object_class_install_property (oclass, nprop, spec);
    nprop++;
  }
}

static int
gst_base_backend_param_flags (int flags) {
  int pflags = 0;

  if (r2i::ParameterMeta::Flags::READ & flags) {
    pflags += G_PARAM_READABLE;
  }

  if (r2i::ParameterMeta::Flags::WRITE & flags) {
    pflags += G_PARAM_WRITABLE;
  }

  return pflags;
}

static GParamSpec *
gst_base_backend_param_to_spec (r2i::ParameterMeta *param) {
  GParamSpec *spec = NULL;

  switch (param->type) {
    case (r2i::ParameterMeta::Type::INTEGER): {
      spec = g_param_spec_int (param->name.c_str (),
                               param->name.c_str (),
                               param->description.c_str (),
                               G_MININT,
                               G_MAXINT, 0, (GParamFlags) gst_base_backend_param_flags (param->flags));
      break;
    }
    case (r2i::ParameterMeta::Type::STRING): {
      spec = g_param_spec_string (param->name.c_str (),
                                  param->name.c_str (),
                                  param->description.c_str (),
                                  NULL, (GParamFlags) gst_base_backend_param_flags (param->flags));
      break;
    }
    case (r2i::ParameterMeta::Type::DOUBLE): {
      spec = g_param_spec_double (param->name.c_str (),
                                  param->name.c_str (),
                                  param->description.c_str (),
                                  -G_MAXDOUBLE,
                                  G_MAXDOUBLE, DOUBLE_PROPERTY_DEFAULT_VALUE,
                                  (GParamFlags) gst_base_backend_param_flags (param->flags));
      break;
    }
#if GST_VERSION_MINOR >= 14
    case (r2i::ParameterMeta::Type::VECTOR): {
      spec = gst_param_spec_array (param->name.c_str(),
                                   param->name.c_str(),
                                   param->description.c_str(),
                                   g_param_spec_string (param->name.c_str (),
                                       param->name.c_str (),
                                       param->description.c_str (),
                                       NULL, (GParamFlags) gst_base_backend_param_flags (param->flags)),
                                   (GParamFlags) gst_base_backend_param_flags (param->flags));
      break;
    }
#endif
    default:
      g_return_val_if_reached (NULL);
  }
  return spec;
}

void
gst_base_backend_set_property (GObject *object, guint property_id,
                          const GValue *value, GParamSpec *pspec) {
  GstBaseBackend *self = GST_BASE_BACKEND (object);
  GstBaseBackendPrivate *priv = GST_BASE_BACKEND_PRIVATE (self);
  InferenceProperty *property;

  GST_DEBUG_OBJECT (self, "set_property");

  g_mutex_lock (&priv->backend_mutex);
  if (priv->backend_started) {
    switch (pspec->value_type) {
      case G_TYPE_STRING:
        priv->params->Set(pspec->name, g_value_get_string(value));
        break;
      case G_TYPE_INT:
        priv->params->Set(pspec->name, g_value_get_int(value));
        break;
      case G_TYPE_DOUBLE:
        priv->params->Set(pspec->name, g_value_get_double(value));
        break;
      default:
        GST_WARNING_OBJECT (self, "Invalid property type");
        break;
    }
  } else {
    property = new InferenceProperty(value, pspec);
    priv->property_list->push_back(property);
    GST_INFO_OBJECT (self, "Queueing property: %s\n", pspec->name);
  }
  g_mutex_unlock (&priv->backend_mutex);

}

void
gst_base_backend_get_property (GObject *object, guint property_id,
                          GValue *value, GParamSpec *pspec) {
  GstBaseBackend *self = GST_BASE_BACKEND (object);
  GstBaseBackendPrivate *priv = GST_BASE_BACKEND_PRIVATE (self);
  int int_buffer;
  double double_buffer;
  std::string string_buffer;
  GST_DEBUG_OBJECT (self, "get_property");

  if (NULL != priv->params ) {
    switch (pspec->value_type) {
      case G_TYPE_STRING:
        priv->params->Get (pspec->name, string_buffer);
        g_value_set_string (value, string_buffer.c_str());
        break;
      case G_TYPE_INT:
        priv->params->Get (pspec->name, int_buffer);
        g_value_set_int (value, int_buffer);
        break;
      case G_TYPE_DOUBLE:
        priv->params->Get (pspec->name, double_buffer);
        g_value_set_double (value, double_buffer);
        break;
    }
  }
}

gboolean
gst_base_backend_start (GstBaseBackend *self, const gchar *model_location,
                   GError **err) {
  GstBaseBackendPrivate *priv = GST_BASE_BACKEND_PRIVATE (self);
  r2i::RuntimeError error;
  InferenceProperty *property;
  std::list<InferenceProperty *>::iterator property_it;
  static std::vector<r2i::ParameterMeta> params;
  static std::vector<r2i::ParameterMeta>::iterator param_it;

  g_return_val_if_fail (priv, FALSE);
  g_return_val_if_fail (model_location, FALSE);
  g_return_val_if_fail (err, FALSE);


  if (!priv->backend_created) {
    priv->factory = r2i::IFrameworkFactory::MakeFactory (priv->code,
                    error);
    if (error.IsError ()) {
      GST_ERROR_OBJECT (self, "Failed to start the backend library");
      goto error;
    }

    priv->engine = priv->factory->MakeEngine (error);
    if (error.IsError ()) {
      GST_ERROR_OBJECT (self, "Failed to start the backend engine");
      goto error;
    }

    priv->loader = priv->factory->MakeLoader (error);
    if (error.IsError ()) {
      GST_ERROR_OBJECT (self, "Failed to start the model loader");
      goto error;
    }

    priv->model = priv->loader->Load (model_location, error);
    if (error.IsError ()) {
      GST_ERROR_OBJECT (self, "Failed to load model");
      goto error;
    }

    error = priv->engine->SetModel (priv->model);
    if (error.IsError ()) {
      GST_ERROR_OBJECT (self, "Failed to set model to engine");
      goto error;
    }

    priv->params = priv->factory->MakeParameters (error);
    if (error.IsError ()) {
      GST_ERROR_OBJECT (self, "Failed to set get parameters for backend");
      goto error;
    }
    error = priv->params->Configure(priv->engine, priv->model);
    if (error.IsError ()) {
      GST_ERROR_OBJECT (self, "Failed to configure mode to backend");
      goto error;
    }
    error = priv->params->List (params);
    if (error.IsError ()) {
      GST_ERROR_OBJECT (self, "Failed to list the backend parameters");
      goto error;
    }
    priv->backend_created = true;
  }

  g_mutex_lock (&priv->backend_mutex);
  for (property_it = priv->property_list->begin();
       property_it != priv->property_list->end(); ++property_it) {
    property = *property_it;
    for (param_it = params.begin(); param_it != params.end(); ++param_it) {
      if (!g_strcmp0(property->get_name(), param_it->name.c_str())) {
        if (r2i::ParameterMeta::Flags::WRITE_BEFORE_START & param_it->flags) {
          property->apply_inference_property(self, priv->params, error);
          property->~InferenceProperty();
          priv->property_list->erase(property_it--);
          if (error.IsError ()) {
            GST_ERROR_OBJECT (self, "Failed to set backend parameters");
	    goto start_error;
          }
        }
        break;
      }
    }
  }

  error = priv->engine->Start ();
  if (error.IsError ()) {
    GST_ERROR_OBJECT (self, "Failed to start the backend engine");
    goto start_error;
  }

  while (!priv->property_list->empty()) {
    property = priv->property_list->front();
    property->apply_inference_property(self, priv->params, error);
    property->~InferenceProperty();
    priv->property_list->pop_front();
    if (error.IsError ()) {
      GST_ERROR_OBJECT (self, "Failed to set backend parameters");
      goto start_error;
    }
  }
  priv->backend_started = true;
  g_mutex_unlock (&priv->backend_mutex);

  return TRUE;

start_error:
  g_mutex_unlock (&priv->backend_mutex);
error:
  g_set_error (err, GST_BASE_BACKEND_ERROR, error.GetCode (),
               "R2Inference Error: (Code:%d) %s", error.GetCode (),
               error.GetDescription ().c_str ());
  return FALSE;
}

gboolean
gst_base_backend_stop (GstBaseBackend *self, GError **err) {
  GstBaseBackendPrivate *priv = GST_BASE_BACKEND_PRIVATE (self);
  r2i::RuntimeError error;

  g_return_val_if_fail (priv, FALSE);
  g_return_val_if_fail (err, FALSE);

  error = priv->engine->Stop ();
  if (error.IsError ()) {
    GST_ERROR_OBJECT (self, "Failed to stop the backend engine");
    goto error;
  }
  return TRUE;

error:
  g_set_error (err, GST_BASE_BACKEND_ERROR, error.GetCode (),
               "R2Inference Error: (Code:%d) %s", error.GetCode (),
               error.GetDescription ().c_str ());
  return FALSE;
}

static r2i::ImageFormat::Id
gst_base_backend_cast_format (GstVideoFormat format) {
  r2i::ImageFormat::Id image_format;

  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
      image_format = r2i::ImageFormat::Id::RGB;
      break;
    case GST_VIDEO_FORMAT_BGR:
      image_format = r2i::ImageFormat::Id::BGR;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      image_format = r2i::ImageFormat::Id::GRAY8;
      break;
    default:
      image_format = r2i::ImageFormat::Id::RGB;
      break;
  }
  return image_format;
}

gboolean
gst_base_backend_process_frame (GstBaseBackend *self, GstVideoFrame *input_frame,
                           gpointer *prediction_data, gsize *prediction_size, GError **err) {
  GstBaseBackendPrivate *priv = GST_BASE_BACKEND_PRIVATE (self);
  std::vector<std::shared_ptr<r2i::IPrediction>> predictions;
  std::shared_ptr < r2i::IFrame > frame;
  r2i::RuntimeError error;
  gint num_outputs = 0;
  gsize extra_size = 0;
  gpointer data = NULL;
  gpointer data_offset = 0;
  gsize size = 0;
  gint i = 0;

  g_return_val_if_fail (priv, FALSE);
  g_return_val_if_fail (input_frame, FALSE);
  g_return_val_if_fail (prediction_data, FALSE);
  g_return_val_if_fail (prediction_size, FALSE);
  g_return_val_if_fail (err, FALSE);

  frame = priv->factory->MakeFrame (error);
  if (error.IsError ()) {
    goto error;
  }

  GST_LOG_OBJECT (self, "Processing Frame of size %d x %d",
                  input_frame->info.width, input_frame->info.height);

  error =
    frame->Configure (input_frame->data[0], input_frame->info.width,
                      input_frame->info.height,
                      gst_base_backend_cast_format(input_frame->info.finfo->format),
                      r2i::DataType::Id::FLOAT);
  if (error.IsError ()) {
    goto error;
  }

  error = priv->engine->Predict (frame, predictions);

  /* We verify it the error is not implemented to keep compatibility with
   backends that do not support multiple predictions */
  if (r2i::RuntimeError::Code::NOT_IMPLEMENTED == error.GetCode()) {
    std::shared_ptr < r2i::IPrediction > prediction;

    prediction = priv->engine->Predict (frame, error);
    predictions.push_back(prediction);
  }

  if (error.IsError ()) {
    goto error;
  }

  num_outputs = predictions.size();
  GST_LOG_OBJECT (self, "Got %d predictions", num_outputs);

  if (0 == num_outputs) {
    error.Set (r2i::RuntimeError::Code::WRONG_ENGINE_STATE,
               "Engine got 0 predictions");
    goto error;
  }

  /* Concatenate all the outputs in a 1D array */
  for (i = 0; i < num_outputs; i++) {
    /* Compute the size including the new tensor and reallocate memory */
    extra_size = predictions[i]->GetResultSize ();
    data = g_realloc (data, size + extra_size);

    /* Compute the offset to concatenate the new tensor */
    data_offset = (void *) ((gsize) data + size);
    /* Could we avoid memory copy ?*/
    memcpy (data_offset, predictions[i]->GetResultData (), extra_size);
    size += extra_size;
  }

  *prediction_size = size;
  *prediction_data = data;

  GST_LOG_OBJECT (self, "Size of prediction %p is %lu",
                  *prediction_data, *prediction_size);

  frame = nullptr;
  predictions.clear();

  return TRUE;
error:
  g_set_error (err, GST_BASE_BACKEND_ERROR, error.GetCode (),
               "R2Inference Error: (Code:%d) %s", error.GetCode (),
               error.GetDescription ().c_str ());
  return FALSE;
}

gboolean
gst_base_backend_set_framework_code (GstBaseBackend *backend, r2i::FrameworkCode code) {
  GstBaseBackendPrivate *priv = GST_BASE_BACKEND_PRIVATE (backend);
  g_return_val_if_fail (priv, FALSE);

  priv->code = code;
  return TRUE;

}

guint
gst_base_backend_get_framework_code (GstBaseBackend *backend) {
  GstBaseBackendPrivate *priv = GST_BASE_BACKEND_PRIVATE (backend);
  g_return_val_if_fail (priv, -1);

  return priv->code;
}

GQuark
gst_base_backend_error_quark(void) {
  static GQuark q = 0;

  if (0 == q) {
    q = g_quark_from_static_string("gst-backend-error-quark");
  }

  return q;
}
