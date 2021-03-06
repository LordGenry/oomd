/*
 * Copyright (C) 2018-present, Facebook, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

#include "oomd/Oomd.h"

using namespace Oomd;
using namespace testing;

constexpr auto kCgroupDataDir = "oomd/fixtures/cgroup";

class OomdTest : public ::testing::Test {
 public:
  OomdTest() {
    oomd = std::make_unique<::Oomd::Oomd>(nullptr, 5, kCgroupDataDir);
  }

  std::string cgroup_path{kCgroupDataDir};
  OomdContext ctx;
  std::unique_ptr<::Oomd::Oomd> oomd{nullptr};
  CgroupPath service1{cgroup_path, "system.slice/service1.service"};
  CgroupPath service2{cgroup_path, "system.slice/service2.service"};
  CgroupPath service3{cgroup_path, "system.slice/service3.service"};
  CgroupPath service4{cgroup_path, "system.slice/service4.service"};
  CgroupPath slice1{cgroup_path, "system.slice/slice1.slice"};
  CgroupPath workload_service1{cgroup_path, "workload.slice/service1.service"};
};

TEST_F(OomdTest, OomdContextUpdate) {
  EXPECT_EQ(ctx.cgroups().size(), 0);

  std::unordered_set<CgroupPath> cgroups;
  cgroups.emplace(CgroupPath(cgroup_path, "system.slice/*"));
  oomd->updateContext(cgroups, ctx);

  EXPECT_EQ(ctx.cgroups().size(), 5);

  EXPECT_TRUE(ctx.hasCgroupContext(service1));
  EXPECT_TRUE(ctx.hasCgroupContext(service2));
  EXPECT_TRUE(ctx.hasCgroupContext(service3));
  EXPECT_TRUE(ctx.hasCgroupContext(service4));
  EXPECT_TRUE(ctx.hasCgroupContext(slice1));
}

TEST_F(OomdTest, OomdContextMultipleUpdates) {
  std::unordered_set<CgroupPath> cgroups;
  cgroups.emplace(CgroupPath(cgroup_path, "system.slice/*"));
  oomd->updateContext(cgroups, ctx);

  for (int i = 0; i < 3; i++) {
    int64_t average = ctx.getCgroupContext(service1).average_usage;
    oomd->updateContext(cgroups, ctx);

    // We expect the avg usage to slowly converge from 0 -> true avg
    // b/c of AVERAGE_SIZE_DECAY
    EXPECT_GT(ctx.getCgroupContext(service1).average_usage, average);
  }
}

TEST_F(OomdTest, OomdContextUpdateMultiCgroup) {
  EXPECT_EQ(ctx.cgroups().size(), 0);

  std::unordered_set<CgroupPath> cgroups;
  cgroups.emplace(CgroupPath(cgroup_path, "system.slice/*"));
  cgroups.emplace(CgroupPath(cgroup_path, "workload.slice/*"));
  oomd->updateContext(cgroups, ctx);

  EXPECT_EQ(ctx.cgroups().size(), 6);

  EXPECT_TRUE(ctx.hasCgroupContext(service1));
  EXPECT_TRUE(ctx.hasCgroupContext(service2));
  EXPECT_TRUE(ctx.hasCgroupContext(service3));
  EXPECT_TRUE(ctx.hasCgroupContext(service4));
  EXPECT_TRUE(ctx.hasCgroupContext(slice1));
  EXPECT_TRUE(ctx.hasCgroupContext(workload_service1));
}

TEST_F(OomdTest, OomdContextUpdateMultiCgroupWildcard) {
  EXPECT_EQ(ctx.cgroups().size(), 0);

  std::unordered_set<CgroupPath> cgroups;
  cgroups.emplace(CgroupPath(cgroup_path, "*.slice/*"));
  cgroups.emplace(CgroupPath(cgroup_path, "workload.slice/*"));
  oomd->updateContext(cgroups, ctx);

  EXPECT_EQ(ctx.cgroups().size(), 6);

  EXPECT_TRUE(ctx.hasCgroupContext(service1));
  EXPECT_TRUE(ctx.hasCgroupContext(service2));
  EXPECT_TRUE(ctx.hasCgroupContext(service3));
  EXPECT_TRUE(ctx.hasCgroupContext(service4));
  EXPECT_TRUE(ctx.hasCgroupContext(slice1));
  EXPECT_TRUE(ctx.hasCgroupContext(workload_service1));
}

TEST_F(OomdTest, CalculateProtectionOverage) {
  std::unordered_set<CgroupPath> cgroups;
  cgroups.emplace(CgroupPath(cgroup_path, "system.slice/*"));
  oomd->updateContext(cgroups, ctx);

  auto s1_ctx = ctx.getCgroupContext(service1);
  auto s2_ctx = ctx.getCgroupContext(service2);
  auto s3_ctx = ctx.getCgroupContext(service3);
  auto s4_ctx = ctx.getCgroupContext(service4);
  auto sl1_ctx = ctx.getCgroupContext(slice1);

  EXPECT_LT(s1_ctx.protection_overage, s2_ctx.protection_overage);
  EXPECT_LT(s1_ctx.protection_overage, s3_ctx.protection_overage);
  EXPECT_LT(s1_ctx.protection_overage, s4_ctx.protection_overage);
  EXPECT_LT(s1_ctx.protection_overage, sl1_ctx.protection_overage);
  EXPECT_EQ(s2_ctx.protection_overage, s3_ctx.protection_overage);
  EXPECT_EQ(s2_ctx.protection_overage, s4_ctx.protection_overage);
  EXPECT_EQ(s2_ctx.protection_overage, sl1_ctx.protection_overage);
}

TEST_F(OomdTest, CalculateProtectionOverageContrived) {
  const std::string contrived_cgroup_path =
      cgroup_path + "/protection_overage.fakeroot";

  std::unordered_set<CgroupPath> cgroups;
  cgroups.emplace(CgroupPath(contrived_cgroup_path, "*/*"));
  // We're manually adding in ancestors here b/c Oomd::Oomd usually does
  // this for us and we're not using the real constructor code path
  cgroups.emplace(CgroupPath(contrived_cgroup_path, "*"));
  oomd->updateContext(cgroups, ctx);

  ctx.dump();

  int64_t A = ctx.getCgroupContext(CgroupPath(contrived_cgroup_path, "A"))
                  .protection_overage;
  int64_t A1 = ctx.getCgroupContext(CgroupPath(contrived_cgroup_path, "A/A1"))
                   .protection_overage;
  int64_t A2 = ctx.getCgroupContext(CgroupPath(contrived_cgroup_path, "A/A2"))
                   .protection_overage;
  int64_t B = ctx.getCgroupContext(CgroupPath(contrived_cgroup_path, "B"))
                  .protection_overage;
  int64_t B1 = ctx.getCgroupContext(CgroupPath(contrived_cgroup_path, "B/B1"))
                   .protection_overage;
  int64_t B2 = ctx.getCgroupContext(CgroupPath(contrived_cgroup_path, "B/B2"))
                   .protection_overage;

  EXPECT_EQ(A, 2l << 30);
  EXPECT_EQ(B, 3l << 30);

  // Hierarchy is B1 > B2 >= A1 > A2
  EXPECT_GT(A1, A2);
  EXPECT_EQ(B2, A1);
  EXPECT_GT(B1, B2);
}
