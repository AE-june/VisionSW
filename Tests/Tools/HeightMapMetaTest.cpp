#include <gtest/gtest.h>
#include "HeightMapSidecar.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include <cmath>

using namespace vision;

// ── 헬퍼 ─────────────────────────────────────────────────────────────────

static std::string tmpPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

static std::shared_ptr<HeightMap> makeHMWithMeta() {
    auto hm = std::make_shared<HeightMap>();
    hm->width      = 10;
    hm->height     = 8;
    hm->channels   = 1;
    hm->xResMm     = 0.063f;
    hm->yResMm     = 0.033f;
    hm->zResMm     = 0.00105f;
    hm->zZeroCount = 0.f;      // zZero=0 (D-1 B-plan)
    hm->originCol  = 3.5f;
    hm->originRow  = 2.0f;
    hm->frameId    = "test_frame_123";
    hm->data.assign(static_cast<size_t>(hm->width) * hm->height, 5000.f);
    return hm;
}

// ── V2 불변량: 사이드카 쓰기 후 읽기 → 메타 모든 필드 복원 ──────────────

TEST(HeightMapMetaTest, WriteReadRoundtrip) {
    const std::string imgPath = tmpPath("hmtest_roundtrip.png");
    const std::string metaPath = imgPath + ".meta.json";

    auto hm = makeHMWithMeta();
    ASSERT_TRUE(writeSidecar(imgPath, *hm));

    auto meta = readSidecar(imgPath);
    ASSERT_TRUE(meta.has_value());

    EXPECT_NEAR(meta->xResMm,     hm->xResMm,     1e-6f);
    EXPECT_NEAR(meta->yResMm,     hm->yResMm,     1e-6f);
    EXPECT_NEAR(meta->zResMm,     hm->zResMm,     1e-8f);
    EXPECT_NEAR(meta->zZeroCount, hm->zZeroCount, 1e-6f);
    EXPECT_NEAR(meta->originCol,  hm->originCol,  1e-6f);
    EXPECT_NEAR(meta->originRow,  hm->originRow,  1e-6f);
    EXPECT_EQ  (meta->width,      hm->width);
    EXPECT_EQ  (meta->height,     hm->height);
    EXPECT_EQ  (meta->channels,   hm->channels);
    EXPECT_EQ  (meta->frameId,    hm->frameId);

    // 정리
    std::remove(metaPath.c_str());
}

// zZeroCount=0 (B-plan) 정확히 왕복되는지 확인
TEST(HeightMapMetaTest, ZZeroCountZeroRoundtrip) {
    const std::string imgPath = tmpPath("hmtest_zzero.png");
    auto hm = makeHMWithMeta();
    hm->zZeroCount = 0.f;

    ASSERT_TRUE(writeSidecar(imgPath, *hm));
    auto meta = readSidecar(imgPath);
    ASSERT_TRUE(meta.has_value());
    EXPECT_FLOAT_EQ(meta->zZeroCount, 0.f);

    std::remove((imgPath + ".meta.json").c_str());
}

// zZeroCount=32768 (legacy PNG) 정확히 왕복
TEST(HeightMapMetaTest, ZZeroCountLegacyRoundtrip) {
    const std::string imgPath = tmpPath("hmtest_legacy.png");
    auto hm = makeHMWithMeta();
    hm->zZeroCount = 32768.f;

    ASSERT_TRUE(writeSidecar(imgPath, *hm));
    auto meta = readSidecar(imgPath);
    ASSERT_TRUE(meta.has_value());
    EXPECT_NEAR(meta->zZeroCount, 32768.f, 1e-3f);

    std::remove((imgPath + ".meta.json").c_str());
}

// 사이드카 없으면 nullopt
TEST(HeightMapMetaTest, MissingSidecarReturnsNullopt) {
    const std::string imgPath = tmpPath("hmtest_nosidecar.png");
    std::remove((imgPath + ".meta.json").c_str());  // 혹시 있으면 삭제
    auto meta = readSidecar(imgPath);
    EXPECT_FALSE(meta.has_value());
}

// frameId가 빈 문자열이어도 왕복
TEST(HeightMapMetaTest, EmptyFrameIdRoundtrip) {
    const std::string imgPath = tmpPath("hmtest_noframe.png");
    auto hm = makeHMWithMeta();
    hm->frameId = "";

    ASSERT_TRUE(writeSidecar(imgPath, *hm));
    auto meta = readSidecar(imgPath);
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->frameId, "");

    std::remove((imgPath + ".meta.json").c_str());
}

// applySidecar: 로드 후 메타 적용 → zMm 계산이 달라짐 확인
TEST(HeightMapMetaTest, ApplySidecarUpdatesZMm) {
    // raw=1000, zRes=0.001, zZero=0 → zMm = 1.0
    auto hm = std::make_shared<HeightMap>();
    hm->width = 1; hm->height = 1; hm->channels = 1;
    hm->xResMm = 1.f; hm->yResMm = 1.f;
    hm->zResMm = 0.001f; hm->zZeroCount = 32768.f;  // 로더 기본값
    hm->data = {1000.f};

    // zMm = (1000 - 32768) * 0.001 = -31.768 (잘못됨)
    double zmBefore = hm->zMm(0, 0);
    EXPECT_NEAR(zmBefore, (1000.f - 32768.f) * 0.001f, 1e-9);

    // 사이드카 메타 적용: zZeroCount=0으로 수정
    HeightMapMeta meta;
    meta.xResMm = 1.f; meta.yResMm = 1.f;
    meta.zResMm = 0.001f; meta.zZeroCount = 0.f;
    meta.originCol = 0.f; meta.originRow = 0.f;
    meta.width = 1; meta.height = 1; meta.channels = 1;
    applySidecar(*hm, meta);

    // zMm = (1000 - 0) * 0.001 = 1.0 (올바름)
    EXPECT_NEAR(hm->zMm(0, 0), 1.0, 1e-9);
}
