#include "pstore_Column.h"

#include "pstore/column.h"
#include "pstore-jni.h"

JNIEXPORT jlong JNICALL Java_pstore_Column_create(JNIEnv *env, jobject obj, jstring name, jlong id, jint type)
{
	const char *name0;
	void *ptr;

	name0 = (*env)->GetStringUTFChars(env, name, NULL);
	if (!name0)
		return PTR_TO_LONG(NULL);

	ptr = pstore_column__new(name0, id, type);

	(*env)->ReleaseStringUTFChars(env, name, name0);

	return PTR_TO_LONG(ptr);
}
