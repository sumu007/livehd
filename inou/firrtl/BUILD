load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_java//java:defs.bzl", "java_library")
load("@rules_proto//proto:defs.bzl", "proto_library")


proto_library(
    name = "firrtl_proto",
    srcs = ["firrtl.proto"],
)

cc_proto_library(
    name = "firrtl_cc_proto",
    deps = [":firrtl_proto"],
)

cc_library(
    name = "inou_firrtl_cpp",
    srcs = [
        "inou_firrtl.cpp",
        "fir_to_lnast.cpp",
        "lnast_to_fir.cpp",
        "inou_firrtl.hpp",
    ],
    hdrs = ["inou_firrtl.hpp"],
    #FIXME: The .pb.* files above have to be manually copied into
    # the inou/firrtl/ directory. To get them, in the livehd directory
    # run "bazel build //inou/firrtl:firrtl_cc_proto". Then copy
    # the two .pb files from livehd/bazel-bin/inou/firrtl/ into the
    # livehd/inou/firrtl/ directory.
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = [
        ":firrtl_cc_proto",
        "//pass/common:pass",
    ],
)
