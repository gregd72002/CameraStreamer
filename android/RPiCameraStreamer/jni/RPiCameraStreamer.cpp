#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <pthread.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

/*
 * These macros provide a way to store the native pointer to CustomData, which might be 32 or 64 bits, into
 * a jlong, which is always 64 bits, without warnings.
 */
#if GLIB_SIZEOF_VOID_P == 8
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(*env).GetLongField ( thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env).SetLongField ( thiz, fieldID, (jlong)data)
#else
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(jint)(*env).GetLongField ( thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env).SetLongField ( thiz, fieldID, (jlong)(jint)data)
#endif

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  jobject app;            /* Application instance, used to call its methods. A global reference is kept. */
  GstElement *pipeline;   /* The running pipeline */
  GMainContext *context;  /* GLib context used to run the main loop */
  GMainLoop *main_loop;   /* GLib main loop */
  GstElement *video_sink; /* The video sink element which receives XOverlay commands */
  ANativeWindow *native_window; /* The Android native window where video will be rendered */
} CustomData;

/* These global variables cache values which are not changing during execution */
static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;
static jmethodID set_message_method_id;
static jmethodID set_error_method_id;
static jmethodID notify_state_method_id;
static jmethodID on_gstreamer_initialized_method_id;

static unsigned char rpi_ip[4];
static unsigned int rpi_port;
/*
 * Private methods
 */

/* Register this thread with the VM */
static JNIEnv *attach_current_thread (void) {
  JNIEnv *env;
  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", g_thread_self ());
  args.version = JNI_VERSION_1_4;
  args.name = NULL;
  args.group = NULL;
  GST_DEBUG ("Attaching thread0");
  if (!java_vm) GST_DEBUG ("java_vm not set");
  if ((*java_vm).AttachCurrentThread (&env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }
  GST_DEBUG ("Attaching thread1");
  return env;
}

/* Unregister this thread from the VM */
static void detach_current_thread (void *env) {
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm).DetachCurrentThread ();
}

/* Retrieve the JNI environment for this thread */
static JNIEnv *get_jni_env (void) {
  JNIEnv *env;
	GST_DEBUG ("Getting ENV");
  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
		GST_DEBUG ("ENV got??");
    env = attach_current_thread ();
    GST_DEBUG ("Thread attached");
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

/* Change the content of the UI's TextView */
static void set_ui_message (const gchar *message, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Setting message to: %s", message);
  jstring jmessage = (*env).NewStringUTF( message);
  (*env).CallVoidMethod ( data->app, set_message_method_id, jmessage);
  if ((*env).ExceptionCheck ()) {
    GST_ERROR ("Failed to call Java method");
    (*env).ExceptionClear ();
  }
  (*env).DeleteLocalRef ( jmessage);
}

/* Change the content of the UI's TextView */
static void set_error (const jint type, const gchar *message, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Setting error to: %s", message);
  jstring jmessage = (*env).NewStringUTF( message);
  (*env).CallVoidMethod ( data->app, set_error_method_id, type, message);
  if ((*env).ExceptionCheck ()) {
    GST_ERROR ("Failed to call Java method");
    (*env).ExceptionClear ();
  }
  (*env).DeleteLocalRef ( jmessage);
}


static void notify_state (int state, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Notify state to: %i", state);
  jint s = state;

  (*env).CallVoidMethod ( data->app, notify_state_method_id, s);
  if ((*env).ExceptionCheck ()) {
    GST_ERROR ("Failed to call Java method notify_state");
    (*env).ExceptionClear ();
  }
}

/* Retrieve errors from the bus and show them on the UI */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
  gchar *message_string;

  gst_message_parse_error (msg, &err, &debug_info);
  message_string = g_strdup_printf ("Error received from element %s: %s", GST_OBJECT_NAME (msg->src), err->message);
  g_clear_error (&err);
  g_free (debug_info);
  set_error(1,message_string, data);
  g_free (message_string);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  notify_state(0,data);
}

/* Notify UI about pipeline state changes */
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    gchar *message = g_strdup_printf("Pipeline status: %s", gst_element_state_get_name(new_state));
    //set_ui_message(message, data);
    int state = 0;
    switch (new_state) {
    	case GST_STATE_VOID_PENDING: state = 0; break;
    	case GST_STATE_NULL:  state = 1; break;
    	case GST_STATE_READY:  state = 2; break;
    	case GST_STATE_PAUSED: state = 3; break;
    	case GST_STATE_PLAYING: state = 4; break;
    	default: state = -1;
    }
    notify_state(state,data);
    g_free (message);
  }
}

/* Check if all conditions are met to report GStreamer as initialized.
 * These conditions will change depending on the application */
static void check_initialization_complete (CustomData *data) {

  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("ENV ok");
  if (data->native_window && data->main_loop) {
    GST_DEBUG ("Initialization complete, notifying application. native_window:%p main_loop:%p", data->native_window, data->main_loop);

    /* The main loop is running and we received a native window, inform the sink about it */
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (data->video_sink), (guintptr)data->native_window);

    (*env).CallVoidMethod ( data->app, on_gstreamer_initialized_method_id);
    if ((*env).ExceptionCheck ()) {
      GST_ERROR ("Failed to call Java method");
      (*env).ExceptionClear ();
    }

  } else {
	  GST_DEBUG ("Initialization not complete");
  }
}

/* Main method for the native code. This is executed on its own thread. */
static void *app_function (void *userdata) {
  JavaVMAttachArgs args;
  GstBus *bus;
  CustomData *data = (CustomData *)userdata;
  GSource *bus_source;
  GError *error = NULL;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  /* Create our own GLib Main Context and make it the default one */
  data->context = g_main_context_new ();
  g_main_context_push_thread_default(data->context);

  /* Build pipeline */

  char pipeline[256];
  //sprintf(pipeline,"tcpserversrc host=%i.%i.%i.%i port=%i ! gdpdepay ! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink\0",rpi_ip[0],rpi_ip[1],rpi_ip[2],rpi_ip[3],rpi_port);
  sprintf(pipeline,"udpsrc address=%i.%i.%i.%i port=%i ! gdpdepay ! rtph264depay  ! avdec_h264 ! videoconvert ! autovideosink sync=false\0",rpi_ip[0],rpi_ip[1],rpi_ip[2],rpi_ip[3],rpi_port);

  GST_DEBUG("PIPELINE : %s",pipeline);

  data->pipeline = gst_parse_launch(pipeline,&error);

  if (error) {
    gchar *message = g_strdup_printf("Unable to build pipeline: %s", error->message);
    g_clear_error (&error);
    set_ui_message(message, data);
    g_free (message);
    return NULL;
  }

  /* Set the pipeline to READY, so it can already accept a window handle, if we have one */
  gst_element_set_state(data->pipeline, GST_STATE_READY);

  data->video_sink = gst_bin_get_by_interface(GST_BIN(data->pipeline), GST_TYPE_VIDEO_OVERLAY);
  if (!data->video_sink) {
    GST_ERROR ("Could not retrieve video sink");
    return NULL;
  }

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->pipeline);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
  g_source_attach (bus_source, data->context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  gst_object_unref (bus);

  /* Create a GLib Main Loop and set it to run */
  GST_DEBUG ("Entering main loop... (CustomData:%p)", data);
  data->main_loop = g_main_loop_new (data->context, FALSE);
  check_initialization_complete (data);
  g_main_loop_run (data->main_loop);
  GST_DEBUG ("Exited main loop");
  g_main_loop_unref (data->main_loop);
  data->main_loop = NULL;

  /* Free resources */
  g_main_context_pop_thread_default(data->context);
  g_main_context_unref (data->context);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  notify_state(0,data);
  gst_object_unref (data->video_sink);
  gst_object_unref (data->pipeline);
  data->pipeline = NULL;
  data->video_sink = NULL;

  return NULL;
}

/*
 * Java Bindings
 */

static void gst_native_config (JNIEnv* env, jobject thiz, jbyteArray arr, jint port) {
	int i;
	rpi_port = port;
	jsize len = (*env).GetArrayLength(arr);
	jbyte *body = (*env).GetByteArrayElements(arr, 0);
	for (i=0; i<len; i++)
		rpi_ip[i] = body[i];
	(*env).ReleaseByteArrayElements(arr, body, 0);
}

/* Instruct the native code to create its internal data structure, pipeline and thread */
static void gst_native_init (JNIEnv* env, jobject thiz) {
  CustomData *data = g_new0 (CustomData, 1);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, data);
  GST_DEBUG_CATEGORY_INIT (debug_category, "RPiCameraStreamer", 0, "Gregory Dymarek");
  gst_debug_set_threshold_for_name("RPiCameraStreamer", GST_LEVEL_DEBUG);
  GST_DEBUG ("Created CustomData at %p", data);
  data->app = (*env).NewGlobalRef ( thiz);
  GST_DEBUG ("Created GlobalRef for app object at %p", data->app);
}

static void gst_native_start(JNIEnv* env, jobject thiz) {
	  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
	  if (!data) return;
	  pthread_create (&gst_app_thread, NULL, &app_function, data);
}

static void gst_native_stop(JNIEnv* env, jobject thiz) {
	  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
	  if (!data) return;
	  GST_DEBUG ("Quitting main loop...");
	  g_main_loop_quit (data->main_loop);
	  GST_DEBUG ("Waiting for thread to finish...");
	  pthread_join (gst_app_thread, NULL);
}

/* Quit the main loop, remove the native thread and free resources */
static void gst_native_finalize (JNIEnv* env, jobject thiz) {
  gst_native_stop(env,thiz);

  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Deleting GlobalRef for app object at %p", data->app);
  (*env).DeleteGlobalRef ( data->app);
  GST_DEBUG ("Freeing CustomData at %p", data);
  g_free (data);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, NULL);
  GST_DEBUG ("Done finalizing");
}

/* Set pipeline to PLAYING state */
static void gst_native_play (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PLAYING");
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}


/* Static class initializer: retrieve method and field IDs */
static jboolean gst_native_class_init (JNIEnv* env, jclass klass) {
  custom_data_field_id = (*env).GetFieldID ( klass, "native_custom_data", "J");
  set_message_method_id = (*env).GetMethodID ( klass, "setMessage", "(Ljava/lang/String;)V");
  set_error_method_id = (*env).GetMethodID ( klass, "setError", "(ILjava/lang/String;)V");
  notify_state_method_id = (*env).GetMethodID ( klass, "notifyState", "(I)V");
  on_gstreamer_initialized_method_id = (*env).GetMethodID ( klass, "onGStreamerInitialized", "()V");

  if (!custom_data_field_id) {
	  __android_log_print (ANDROID_LOG_ERROR, "RPiCameraStreamer", "The calling class does not implement native_custom_data");
	  return JNI_FALSE;
  }
  if (!set_message_method_id) {
	  __android_log_print (ANDROID_LOG_ERROR, "RPiCameraStreamer", "The calling class does not implement setMessage");
	  return JNI_FALSE;
  }
  if (!notify_state_method_id) {
	  __android_log_print (ANDROID_LOG_ERROR, "RPiCameraStreamer", "The calling class does not implement notifyState");
	  return JNI_FALSE;
  }
  if (!on_gstreamer_initialized_method_id) {
	  __android_log_print (ANDROID_LOG_ERROR, "RPiCameraStreamer", "The calling class does not implement onGStreamerInitialized");
	  return JNI_FALSE;
  }
  if (!set_error_method_id) {
	  __android_log_print (ANDROID_LOG_ERROR, "RPiCameraStreamer", "The calling class does not implement setError");
	  return JNI_FALSE;
  }

  __android_log_print (ANDROID_LOG_ERROR, "RPiCameraStreamer", "gst_native_class_init OK");

  return JNI_TRUE;
}

static void gst_native_surface_init (JNIEnv *env, jobject thiz, jobject surface) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  ANativeWindow *new_native_window = ANativeWindow_fromSurface(env, surface);
  GST_DEBUG ("Received surface %p (native window %p)", surface, new_native_window);

  if (data->native_window) {
	GST_DEBUG ("Checking native window");
    ANativeWindow_release (data->native_window);
    if (data->native_window == new_native_window) {
      GST_DEBUG ("New native window is the same as the previous one %p", data->native_window);
      if (data->video_sink) {
        gst_video_overlay_expose(GST_VIDEO_OVERLAY (data->video_sink));
        gst_video_overlay_expose(GST_VIDEO_OVERLAY (data->video_sink));
      }
      return;
    } else {
      GST_DEBUG ("Released previous native window %p", data->native_window);
      data->native_window = NULL;
    }
  }
  GST_DEBUG ("Native window not set");
  data->native_window = new_native_window;

  //check_initialization_complete (data);
}

static void gst_native_surface_finalize (JNIEnv *env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Releasing Native Window %p", data->native_window);

  if (data->video_sink) {
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (data->video_sink), (guintptr)NULL);
    if (data->pipeline) gst_element_set_state (data->pipeline, GST_STATE_READY);
  }

  if (data->native_window) ANativeWindow_release (data->native_window);
  data->native_window = NULL;
}

/* List of implemented native methods */

static JNINativeMethod native_methods[] = {
  { "nativeInit", "()V", (void *) gst_native_init},
  { "nativeConfig", "([BI)V", (void *) gst_native_config},
  { "nativeFinalize", "()V", (void *) gst_native_finalize},
  { "nativeStart", "()V", (void *) gst_native_start},
  { "nativeStop", "()V", (void *) gst_native_stop},
  { "nativePlay", "()V", (void *) gst_native_play},
  { "nativeSurfaceInit", "(Ljava/lang/Object;)V", (void *) gst_native_surface_init},
  { "nativeSurfaceFinalize", "()V", (void *) gst_native_surface_finalize},
  { "nativeClassInit", "()Z", (void *) gst_native_class_init}
};


/* Library initializer */
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = NULL;

  java_vm = vm;

  if ((*vm).GetEnv( (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "RPiCameraStreamer", "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env).FindClass ( "com/rpicopter/rpicamerastreamer/MainActivity");
  (*env).RegisterNatives ( klass, native_methods, G_N_ELEMENTS(native_methods));

  pthread_key_create (&current_jni_env, detach_current_thread);
  char *version_utf8 = gst_version_string();
  __android_log_print (ANDROID_LOG_VERBOSE, "RPiCameraStreamer", "GSTREAMER VERSION: %s",version_utf8);
  g_free (version_utf8);;
  return JNI_VERSION_1_4;
}
