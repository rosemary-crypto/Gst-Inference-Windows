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

#ifndef __CROP_ELEMENT_H__
#define __CROP_ELEMENT_H__

#include <gst/gst.h>
#include <mutex>
#include <string>


class CropElement {
 public:
  CropElement ();
  bool Validate ();
  GstElement *GetElement ();
  void SetCroppingSize (gint top, gint bottom, gint right, gint left);
  virtual ~ CropElement ();
  virtual const std::string& GetFactory () const = 0;
  virtual GstPad *GetSinkPad () = 0;
  virtual GstPad *GetSrcPad () = 0;
  void Reset ();

 protected:
  virtual void UpdateElement (GstElement *element,
                              gint top,
                              gint bottom,
                              gint right,
                              gint left) = 0;

 private:
  GstElement *element;
  gint top;
  gint bottom;
  gint right;
  gint left;
  std::mutex mutex;
};

#endif //__CROP_ELEMENT_H__
