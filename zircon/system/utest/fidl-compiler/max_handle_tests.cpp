// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

namespace {

class MaxHandlesLibrary : public TestLibrary {
public:
    MaxHandlesLibrary()
        : TestLibrary("max_handles.fidl", R"FIDL(
library fidl.test.maxhandles;

struct OneBool {
  bool b;
};

struct OneHandle {
  handle h;
};

struct HandleArray {
  array<handle>:8 ha;
};

struct NullableHandleArray {
  array<handle?>:8 ha;
};

struct HandleVector {
  vector<handle>:8 hv;
};

struct HandleNullableVector {
  vector<handle>:8? hv;
};

struct UnboundedHandleVector {
  vector<handle> hv;
};

struct HandleStructVector {
  vector<OneHandle>:8 sv;
};

struct HandleTableVector {
  vector<TableWithOneHandle>:8 sv;
};

table TableWithNoMembers {
};

table TableWithOneBool {
  1: bool b;
};

table TableWithOneHandle {
  1: handle h;
};

table TableWithHandleArray {
  1: array<handle>:8 ha;
};

table TableWithNullableHandleArray {
  1: array<handle?>:8 ha;
};

table TableWithHandleVector {
  1: vector<handle>:8 hv;
};

table TableWithUnboundedHandleVector {
  1: vector<handle> hv;
};

table TableWithHandleStructVector {
  1: vector<OneHandle>:8 sv;
};

table TableWithHandleTableVector {
  1: vector<OneHandle>:8 sv;
};

union NoHandleUnion {
  OneBool one_bool;
  uint32 integer;
};

union OneHandleUnion {
  OneHandle one_handle;
  OneBool one_bool;
  uint32 integer;
};

union ManyHandleUnion {
  OneHandle one_handle;
  HandleArray handle_array;
  HandleVector handle_vector;
};

)FIDL") {}
};

static bool simple_structs(void) {
    BEGIN_TEST;

    MaxHandlesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto one_bool = test_library.LookupStruct("OneBool");
    EXPECT_NONNULL(one_bool);
    EXPECT_EQ(one_bool->typeshape.MaxHandles(), 0);

    auto one_handle = test_library.LookupStruct("OneHandle");
    EXPECT_NONNULL(one_handle);
    EXPECT_EQ(one_handle->typeshape.MaxHandles(), 1);

    END_TEST;
}

static bool simple_tables(void) {
    BEGIN_TEST;

    MaxHandlesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto no_members = test_library.LookupTable("TableWithNoMembers");
    EXPECT_NONNULL(no_members);
    EXPECT_EQ(no_members->typeshape.MaxHandles(), 0);

    auto one_bool = test_library.LookupTable("TableWithOneBool");
    EXPECT_NONNULL(one_bool);
    EXPECT_EQ(one_bool->typeshape.MaxHandles(), 0);

    auto one_handle = test_library.LookupTable("TableWithOneHandle");
    EXPECT_NONNULL(one_handle);
    EXPECT_EQ(one_handle->typeshape.MaxHandles(), 1);

    END_TEST;
}

static bool arrays(void) {
    BEGIN_TEST;

    MaxHandlesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto handle_array = test_library.LookupStruct("HandleArray");
    EXPECT_NONNULL(handle_array);
    EXPECT_EQ(handle_array->typeshape.MaxHandles(), 8);

    auto table_with_handle_array = test_library.LookupTable("TableWithHandleArray");
    EXPECT_NONNULL(table_with_handle_array);
    EXPECT_EQ(table_with_handle_array->typeshape.MaxHandles(), 8);

    auto nullable_handle_array = test_library.LookupStruct("NullableHandleArray");
    EXPECT_NONNULL(nullable_handle_array);
    EXPECT_EQ(nullable_handle_array->typeshape.MaxHandles(), 8);

    auto table_with_nullable_handle_array = test_library.LookupTable("TableWithNullableHandleArray");
    EXPECT_NONNULL(table_with_nullable_handle_array);
    EXPECT_EQ(table_with_nullable_handle_array->typeshape.MaxHandles(), 8);

    END_TEST;
}

static bool vectors(void) {
    BEGIN_TEST;

    MaxHandlesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto handle_vector = test_library.LookupStruct("HandleVector");
    EXPECT_NONNULL(handle_vector);
    EXPECT_EQ(handle_vector->typeshape.MaxHandles(), 8);

    auto table_with_handle_vector = test_library.LookupTable("TableWithHandleVector");
    EXPECT_NONNULL(table_with_handle_vector);
    EXPECT_EQ(table_with_handle_vector->typeshape.MaxHandles(), 8);

    auto handle_nullable_vector = test_library.LookupStruct("HandleNullableVector");
    EXPECT_NONNULL(handle_nullable_vector);
    EXPECT_EQ(handle_nullable_vector->typeshape.MaxHandles(), 8);

    auto unbounded_handle_vector = test_library.LookupStruct("UnboundedHandleVector");
    EXPECT_NONNULL(unbounded_handle_vector);
    EXPECT_EQ(unbounded_handle_vector->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());

    auto table_with_unbounded_handle_vector = test_library.LookupTable("TableWithUnboundedHandleVector");
    EXPECT_NONNULL(table_with_unbounded_handle_vector);
    EXPECT_EQ(table_with_unbounded_handle_vector->typeshape.MaxHandles(), std::numeric_limits<uint32_t>::max());

    auto handle_struct_vector = test_library.LookupStruct("HandleStructVector");
    EXPECT_NONNULL(handle_struct_vector);
    EXPECT_EQ(handle_struct_vector->typeshape.MaxHandles(), 8);

    auto handle_table_vector = test_library.LookupStruct("HandleTableVector");
    EXPECT_NONNULL(handle_table_vector);
    EXPECT_EQ(handle_table_vector->typeshape.MaxHandles(), 8);

    auto table_with_handle_struct_vector = test_library.LookupTable("TableWithHandleStructVector");
    EXPECT_NONNULL(table_with_handle_struct_vector);
    EXPECT_EQ(table_with_handle_struct_vector->typeshape.MaxHandles(), 8);

    END_TEST;
}

static bool unions(void) {
    BEGIN_TEST;

    MaxHandlesLibrary test_library;
    EXPECT_TRUE(test_library.Compile());

    auto no_handle_union = test_library.LookupUnion("NoHandleUnion");
    EXPECT_NONNULL(no_handle_union);
    EXPECT_EQ(no_handle_union->typeshape.MaxHandles(), 0);

    auto one_handle_union = test_library.LookupUnion("OneHandleUnion");
    EXPECT_NONNULL(one_handle_union);
    EXPECT_EQ(one_handle_union->typeshape.MaxHandles(), 1);

    auto many_handle_union = test_library.LookupUnion("ManyHandleUnion");
    EXPECT_NONNULL(many_handle_union);
    EXPECT_EQ(many_handle_union->typeshape.MaxHandles(), 8);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(max_handles_tests);
RUN_TEST(simple_structs);
RUN_TEST(simple_tables);
RUN_TEST(arrays);
RUN_TEST(vectors);
RUN_TEST(unions);
END_TEST_CASE(max_handles_tests);
