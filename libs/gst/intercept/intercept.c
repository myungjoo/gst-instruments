#include "intercept.h"
#include <dlfcn.h>
#include <stdio.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/gstpad.h>
#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h> 
#include <stdlib.h>

#include <signal.h>
#include <time.h>

#include <mach/mach_init.h>
#include <mach/thread_act.h>
#include <mach/mach_port.h>

static FILE *output = NULL;

static guint64 get_cpu_time (thread_port_t thread) {
  mach_msg_type_number_t count = THREAD_EXTENDED_INFO_COUNT;
  thread_extended_info_data_t info;
  
  int kr = thread_info (thread, THREAD_EXTENDED_INFO, (thread_info_t) &info, &count);
  if (kr != KERN_SUCCESS) {
    return 0;
  }
  
  return (guint64) info.pth_user_time + info.pth_system_time;
}

void *libgstreamer = NULL;
GMutex output_mutex;

GstFlowReturn (*gst_pad_push_orig) (GstPad *pad, GstBuffer *buffer) = NULL;
GstFlowReturn (*gst_pad_push_list_orig) (GstPad *pad, GstBufferList *list) = NULL;
GstFlowReturn (*gst_pad_pull_range_orig) (GstPad *pad, guint64 offset, guint size, GstBuffer **buffer) = NULL;
gboolean (*gst_pad_push_event_orig) (GstPad *pad, GstEvent *event) = NULL;

void
deinit ()
{
  if (output != stderr && output != stdout)
    fclose (output);
  
  dlclose (libgstreamer);
  // g_mutex_clear(&output_mutex);
}

void *
get_libgstreamer ()
{
  atexit (deinit);
  
  if (libgstreamer == NULL)
    libgstreamer = dlopen ("libgstreamer-1.0.dylib", RTLD_NOW);
  
  const gchar * output_filename = g_getenv ("GST_INTERCEPT_OUTPUT_FILE");
  if (output_filename)
  {
    output = fopen (output_filename, "wt");
  }
  
  g_mutex_init (&output_mutex);
  
  return libgstreamer;
}

void
gst_debug_log (GstDebugCategory *category, GstDebugLevel level, const gchar *file, const gchar *function, gint line, GObject *object, const gchar *format, ...)//va_list args)
{
  if (g_ascii_strcasecmp (category->name, "task") == 0)
  {
    get_libgstreamer ();
    
    g_mutex_lock (&output_mutex);
    fprintf (output, "task %p\n", g_thread_self ());
    fflush (output);
    g_mutex_unlock (&output_mutex);
  }
}

gpointer get_downstack_element(gpointer pad)
{
  gpointer element = pad;
  do
  {
    gpointer peer = GST_PAD_PEER (element);
    if (peer)
      element = GST_PAD_PARENT (peer);
    else
      return NULL;
  }
  while (!GST_IS_ELEMENT (element));
  
  return element;
}

GstFlowReturn
gst_pad_push (GstPad *pad, GstBuffer *buffer)
{
  GstFlowReturn result;
  
  if (gst_pad_push_orig == NULL)
  {
    gst_pad_push_orig = dlsym (get_libgstreamer (), "gst_pad_push");
    
    if (gst_pad_push_orig == NULL)
    {
      GST_ERROR ("can not link to gst_pad_push\n");
      return GST_FLOW_CUSTOM_ERROR;
    }
    else
    {
      GST_ERROR ("gst_pad_push linked: %p\n", gst_pad_push_orig);
    }
  }
  
  thread_port_t thread = mach_thread_self ();
  
  gpointer element_from = gst_pad_get_parent_element (pad);
  gpointer element = get_downstack_element (pad);
  
  guint64 start = get_cpu_time (thread);
  
  g_mutex_lock(&output_mutex);
  fprintf (output, "element-entered %p %s %p %s %p %" G_GUINT64_FORMAT "\n", g_thread_self (), GST_ELEMENT_NAME(element_from), element_from, GST_ELEMENT_NAME(element), element, start);
  fflush (output);
  g_mutex_unlock(&output_mutex);
  
  result = gst_pad_push_orig (pad, buffer);
  
  guint64 end = get_cpu_time (thread);
  guint64 duration = end - start;
  mach_port_deallocate (mach_task_self (), thread);
  
  g_mutex_lock(&output_mutex);
  fprintf (output, "element-exited %p %s %p %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT "\n", g_thread_self (), GST_ELEMENT_NAME (element), element, end, duration);
  fflush (output);
  g_mutex_unlock(&output_mutex);

  return result;
}

GstFlowReturn
gst_pad_push_list (GstPad *pad, GstBufferList *list)
{
  GstFlowReturn result;
  
  if (gst_pad_push_list_orig == NULL)
  {
    gst_pad_push_list_orig = dlsym (get_libgstreamer (), "gst_pad_push_list");
    
    if (gst_pad_push_list_orig == NULL)
    {
      GST_ERROR ("can not link to gst_pad_push_list\n");
      return GST_FLOW_CUSTOM_ERROR;
    }
    else
    {
      GST_ERROR ("gst_pad_push_list linked: %p\n", gst_pad_push_orig);
    }
  }
  
  thread_port_t thread = mach_thread_self ();
  
  gpointer element_from = gst_pad_get_parent_element (pad);
  gpointer element = get_downstack_element (pad);
  
  guint64 start = get_cpu_time (thread);
  
  g_mutex_lock(&output_mutex);
  fprintf (output, "element-entered %p %s %p %s %p %" G_GUINT64_FORMAT "\n", g_thread_self (), GST_ELEMENT_NAME(element_from), element_from, GST_ELEMENT_NAME(element), element, start);  fflush (output);
  g_mutex_unlock(&output_mutex);

  result = gst_pad_push_list_orig (pad, list);
  
  guint64 end = get_cpu_time (thread);
  guint64 duration = end - start;
  mach_port_deallocate (mach_task_self (), thread);
  
  g_mutex_lock(&output_mutex);
  fprintf (output, "element-exited %p %s %p %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT "\n", g_thread_self (), GST_ELEMENT_NAME (element), element, end, duration);
  fflush (output);
  g_mutex_unlock(&output_mutex);
  
  return result;
}

gboolean
gst_pad_push_event (GstPad *pad, GstEvent *event)
{
  gboolean result;
  
  if (gst_pad_push_event_orig == NULL)
  {
    gst_pad_push_event_orig = dlsym (get_libgstreamer (), "gst_pad_push_event");
    
    if (gst_pad_push_event_orig == NULL)
    {
      GST_ERROR ("can not link to gst_pad_push_event\n");
      return FALSE;
    }
    else
    {
      GST_ERROR ("gst_pad_push_event linked: %p\n", gst_pad_push_event_orig);
    }
  }
  
  thread_port_t thread = mach_thread_self ();
  
  gpointer element_from = gst_pad_get_parent_element (pad);
  gpointer element = get_downstack_element (pad);
  
  guint64 start = get_cpu_time (thread);
  
  if (element_from && element)
  {
    g_mutex_lock(&output_mutex);
    fprintf (output, "element-entered %p %s %p %s %p %" G_GUINT64_FORMAT "\n", g_thread_self (), GST_ELEMENT_NAME(element_from), element_from, GST_ELEMENT_NAME(element), element, start);
    fflush (output);
    g_mutex_unlock(&output_mutex);
  }
  
  result = gst_pad_push_event_orig (pad, event);
  
  guint64 end = get_cpu_time (thread);
  guint64 duration = end - start;
  mach_port_deallocate (mach_task_self (), thread);
  
  if (element_from && element)
  {
    g_mutex_lock(&output_mutex);
    fprintf (output, "element-exited %p %s %p %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT "\n", g_thread_self (), GST_ELEMENT_NAME (element), element, end, duration);
    fflush (output);
    g_mutex_unlock(&output_mutex);
  }
  
  return result;
}

GstFlowReturn
gst_pad_pull_range (GstPad *pad, guint64 offset, guint size, GstBuffer **buffer)
{
  GstFlowReturn result;
  
  if (gst_pad_pull_range_orig == NULL)
  {
    gst_pad_pull_range_orig = dlsym (get_libgstreamer (), "gst_pad_pull_range");

    if (gst_pad_pull_range_orig == NULL)
    {
      GST_ERROR ("can not link to gst_pad_pull_range\n");
      return GST_FLOW_CUSTOM_ERROR;
    }
    else
    {
      GST_ERROR ("gst_pad_pull_range linked: %p\n", gst_pad_pull_range_orig);
    }
  }
  
  thread_port_t thread = mach_thread_self();
  
  gpointer element_from = gst_pad_get_parent_element (pad);
  gpointer element = get_downstack_element (pad);
  
  guint64 start = get_cpu_time (thread);
  
  g_mutex_lock(&output_mutex);
  fprintf (output, "element-entered %p %s %p %s %p %" G_GUINT64_FORMAT "\n", g_thread_self (), GST_ELEMENT_NAME(element_from), element_from, GST_ELEMENT_NAME(element), element, start);  fflush (output);
  g_mutex_unlock(&output_mutex);

  result = gst_pad_pull_range_orig (pad, offset, size, buffer);
  
  guint64 end = get_cpu_time (thread);
  guint64 duration = end - start;
  mach_port_deallocate (mach_task_self (), thread);
  
  g_mutex_lock(&output_mutex);
  fprintf (output, "element-exited %p %s %p %" G_GUINT64_FORMAT " %" G_GUINT64_FORMAT "\n", g_thread_self (), GST_ELEMENT_NAME (element), element, end, duration);
  fflush (output);
  g_mutex_unlock(&output_mutex);

  return result;
}