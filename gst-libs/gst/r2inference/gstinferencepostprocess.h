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

#include <gst/r2inference/gstvideoinference.h>
#include <gst/r2inference/gstinferencemeta.h>

#ifndef __GST_INFERENCE_POSTPROCESS_H__
#define __GST_INFERENCE_POSTPROCESS_H__

G_BEGIN_DECLS
/**
 * \brief Fill all the data for the boxes
 *
 * \param vi Father object of every architecture
 * \param prediction Value of the prediction
 * \param valid_prediction Check if the prediction is valid
 * \param resulting_boxes The output boxes of the prediction
 * \param elements The number of objects
 * \param obj_thresh Objectness threshold
 * \param prob_thresh Class probability threshold
 * \param iou_thresh Intersection over union threshold
 * \param probabilities Probabilities of each classes
 * \param num_classes The number of classes
 */
gboolean gst_create_boxes (GstVideoInference * vi, const gpointer prediction,
    gboolean * valid_prediction, BBox ** resulting_boxes,
    gint * elements, gfloat obj_thresh, gfloat prob_thresh, gfloat iou_thresh,
    gdouble ** probabilities, gint num_classes);

/**
 * \brief Fill all the data for the boxes
 *
 * \param vi Father object of every architecture
 * \param prediction Value of the prediction
 * \param valid_prediction Check if the prediction is valid
 * \param resulting_boxes The output boxes of the prediction
 * \param elements The number of objects
 * \param obj_thresh Objectness threshold
 * \param prob_thresh Class probability threshold
 * \param iou_thresh Intersection over union threshold
 * \param probabilities Probabilities of each classes
 * \param num_classes The number of classes
 */
gboolean gst_create_boxes_float (GstVideoInference * vi,
    const gpointer prediction, gboolean * valid_prediction,
    BBox ** resulting_boxes, gint * elements, gdouble obj_thresh,
    gdouble prob_thresh, gdouble iou_thresh, gdouble ** probabilities,
    gint num_classes);

/**
 * \brief Create Prediction from box
 *
 * \param vi Father object of every architecture
 * \param box Box used to fill Prediction
 * \param labels_list List with all possible lables
 * \param num_labels The number of posibble labels
 * \param probabilities Probabilities of each classes
 */
GstInferencePrediction *gst_create_prediction_from_box (GstVideoInference * vi,
    BBox * box, gchar **labels_list, gint num_labels, const gdouble * probabilities);

/**
 * \brief Create Classification from prediction data
 *
 * \param vi Father object of every architecture
 * \param prediction Value of the prediction
 * \param predsize Size of the prediction
 * \param labels_list List with all possible lables
 * \param num_labels The number of posibble labels
 */
GstInferenceClassification *gst_create_class_from_prediction (GstVideoInference * vi,
    const gpointer prediction, gsize predsize, gchar **labels_list, gint num_labels);
/**
 * \brief Remove duplicated boxes
 * \param iou_thresh Threshold of iou to consider that a box is duplicated
 * \param boxes Array of bounding boxes
 * \param num_boxes Amount of boxes in the array
 */
void gst_remove_duplicated_boxes (gdouble iou_thresh, BBox * boxes,
    gint * num_boxes);

G_END_DECLS
#endif
