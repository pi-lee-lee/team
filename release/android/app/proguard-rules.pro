# JNI 로 연결되는 메서드는 이름이 바뀌면 네이티브 심볼과 어긋나 UnsatisfiedLinkError 가 된다.
-keepclasseswithmembernames class * {
    native <methods>;
}

-keep class com.example.jnidemo.MainActivity {
    *;
}
