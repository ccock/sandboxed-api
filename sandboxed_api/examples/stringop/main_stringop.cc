// Copyright 2019 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glog/logging.h>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/util/flag.h"
#include "absl/memory/memory.h"
#include "absl/time/time.h"
#include "sandboxed_api/examples/stringop/lib/sandbox.h"
#include "sandboxed_api/examples/stringop/lib/stringop-sapi.sapi.h"
#include "sandboxed_api/examples/stringop/lib/stringop_params.pb.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/vars.h"
#include "sandboxed_api/util/canonical_errors.h"
#include "sandboxed_api/util/status.h"
#include "sandboxed_api/util/status_macros.h"

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::SizeIs;
using ::testing::StrEq;

namespace {

// Tests using a simple transaction (and function pointers):
TEST(StringopTest, ProtobufStringDuplication) {
  sapi::BasicTransaction st(absl::make_unique<StringopSapiSandbox>());
  EXPECT_THAT(st.Run([](sapi::Sandbox* sandbox) -> sapi::Status {
    StringopApi api(sandbox);
    stringop::StringDuplication proto;
    proto.set_input("Hello");
    sapi::v::Proto<stringop::StringDuplication> pp(proto);
    {
      SAPI_ASSIGN_OR_RETURN(int return_value, api.pb_duplicate_string(pp.PtrBoth()));
      TRANSACTION_FAIL_IF_NOT(return_value, "pb_duplicate_string() failed");
    }

    SAPI_ASSIGN_OR_RETURN(auto pb_result, pp.GetMessage());
    LOG(INFO) << "Result PB: " << pb_result.DebugString();
    TRANSACTION_FAIL_IF_NOT(pb_result.output() == "HelloHello",
                            "Incorrect output");
    return sapi::OkStatus();
  }),
              IsOk());
}

TEST(StringopTest, ProtobufStringReversal) {
  StringopSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);

  stringop::StringReverse proto;
  proto.set_input("Hello");
  sapi::v::Proto<stringop::StringReverse> pp(proto);
  SAPI_ASSERT_OK_AND_ASSIGN(int return_value, api.pb_reverse_string(pp.PtrBoth()));
  EXPECT_THAT(return_value, Ne(0)) << "pb_reverse_string() failed";

  SAPI_ASSERT_OK_AND_ASSIGN(auto pb_result, pp.GetMessage());
  LOG(INFO) << "Result PB: " << pb_result.DebugString();
  EXPECT_THAT(pb_result.output(), StrEq("olleH"));
}

TEST(StringopTest, RawStringDuplication) {
  StringopSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);

  sapi::v::LenVal param("0123456789", 10);
  SAPI_ASSERT_OK_AND_ASSIGN(int return_value, api.duplicate_string(param.PtrBoth()));
  EXPECT_THAT(return_value, Eq(1)) << "duplicate_string() failed";

  absl::string_view data(reinterpret_cast<const char*>(param.GetData()),
                         param.GetDataSize());
  EXPECT_THAT(data, SizeIs(20))
      << "duplicate_string() did not return enough data";
  EXPECT_THAT(std::string(data), StrEq("01234567890123456789"));
}

TEST(StringopTest, RawStringReversal) {
  StringopSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  StringopApi api(&sandbox);

  sapi::v::LenVal param("0123456789", 10);
  {
    SAPI_ASSERT_OK_AND_ASSIGN(int return_value, api.reverse_string(param.PtrBoth()));
    EXPECT_THAT(return_value, Eq(1))
        << "reverse_string() returned incorrect value";
    absl::string_view data(reinterpret_cast<const char*>(param.GetData()),
                           param.GetDataSize());
    EXPECT_THAT(param.GetDataSize(), Eq(10))
        << "reverse_string() did not return enough data";
    EXPECT_THAT(std::string(data), StrEq("9876543210"))
        << "reverse_string() did not return the expected data";
  }
  {
    // Let's call it again with different data as argument, reusing the
    // existing LenVal object.
    EXPECT_THAT(param.ResizeData(sandbox.GetRpcChannel(), 16), IsOk());
    memcpy(param.GetData() + 10, "ABCDEF", 6);
    absl::string_view data(reinterpret_cast<const char*>(param.GetData()),
                           param.GetDataSize());
    EXPECT_THAT(data, SizeIs(16)) << "Resize did not behave correctly";
    EXPECT_THAT(std::string(data), StrEq("9876543210ABCDEF"));

    SAPI_ASSERT_OK_AND_ASSIGN(int return_value, api.reverse_string(param.PtrBoth()));
    EXPECT_THAT(return_value, Eq(1))
        << "reverse_string() returned incorrect value";
    data = absl::string_view(reinterpret_cast<const char*>(param.GetData()),
                             param.GetDataSize());
    EXPECT_THAT(std::string(data), StrEq("FEDCBA0123456789"));
  }
}

}  // namespace
