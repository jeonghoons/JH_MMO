# FX 카탈로그 — Paragon 파티클 선별 (MMORPG 공용 FX 라이브러리)

2026-11-10 작성. `Content/Paragon{Greystone,Rampage,Sparrow,Props}`의 Cascade 파티클 시스템 **193개**(Props 0개)를 전부 분석해, 앞으로 늘어날 무기 타입·스킬·피격·사망·레벨업 연출에 쓸 것을 **역할별로 겹치지 않게** 뽑은 목록입니다. 선별 기준은 현재 `SkillTable`이 아니라 **각 에셋이 실제로 무엇을 그리는지**입니다. 방침은 [DESIGN_ART_CONCEPT.md](DESIGN_ART_CONCEPT.md) 6장(Paragon 이펙트를 `Content/FX/`로 Duplicate, 원본 팩 파일은 고치지 않음 — DECISIONS 2026-10-19).

## 1. 판정 근거

에디터 MCP `AssetTools.get_dependencies`로 193개의 참조 머티리얼·스태틱 메시·벡터필드를, `ObjectTools`로 고정 바운딩 박스를 읽고, uasset 이름 테이블에서 이미터 모듈 종류·이미터/소켓 이름을 뽑아 교차 확인했습니다. 표의 "근거" 열이 그 결과입니다.

| 표기 | 뜻 | 쓰임에 미치는 영향 |
|---|---|---|
| **TRAIL** | `TypeDataAnimTrail` — 무기 궤적 | 몽타주에 `AnimNotifyState_Trail` + 캐릭터 **스켈레톤**에 소켓 2개(시작·끝) 필요. 우리 무기는 `hand_r`에 붙는 스태틱 메시라 매니퀸 스켈레톤에 `FX_Weapon_Top/Bottom` 소켓을 추가해야 함(무기 타입별 길이는 `jm_kit/items.json`의 `attach`에서) |
| **RIBBON** | `TypeDataRibbon` — 움직이는 컴포넌트 뒤 리본 | 투사체 액터에 붙이면 그대로 동작 |
| **BONESOCK** | `LocationBoneSocket` — 특정 본 이름에 묶임 | 이름이 Paragon 본(`C_brow_mid`, `chest_playe`, `Hand L Index`, `arrow_nock`…)이면 매니퀸에서 **빈 이미터** → 그 이미터만 지우거나 본 이름 교체 |
| **COLL/EVT** | GPU 충돌·이벤트 스폰 | 지면이 있어야 함 |
| **DELAY** | 이미터 지연 — 다단계 연출 | 타격 시점(`HitDelayMs`)과 맞출 때 지연값 확인 |
| **bb** | 고정 바운딩(cm, X×Y×Z 또는 한 변) | `FxBaseRadius` 초기값 = bb/2 |
| **Camera** | `SM_Camera_Mesh`·`M_Lens_*`·`M_ScreenDistortion_*` | 1인칭 카메라 앞 화면 효과(MOBA) → 3인칭 MMORPG에선 제외 |
| **Water** | `PM_ShallowWater`·`M_WaterImpact_*`·`M_Footfall_Ripple` | 얕은 물 Physical Material 지형에서만 의미 → 보류 |

속성 색: Greystone = 주황·노랑 플라즈마(빛·화염), Sparrow = 금색 불꽃(빛·화염), Rampage = 회색 바위·연기(대지), `Rampage_v001_IceBlue` = 눈·얼음(냉기), `Sparrow/Skins/Rogue` = 보라 그림자 덩굴(암흑). 속성 변형은 **리컬러만 다른 것은 빼고 고유 형상이 있는 것만**(냉기 8·암흑 2) 넣었습니다(2026-11-10 사용자 결정). 리컬러는 머티리얼 파라미터로 대신합니다.

모든 에셋은 **Cascade**입니다(UE 5.8에서 실행됨, 현재 2개 사용 중). Niagara 변환은 하지 않습니다 — 필요해지면 "Convert to Niagara"로 시스템 단위 변환, AnimTrail 타입은 수동 재작성.

## 2. 네이밍·위치

- 복제본: `Content/FX/<분류>/P_<역할>_<이름>` — 분류 폴더 7개 `Trail`·`Hit`·`Proj`·`Cast`·`AoE`·`Buff`·`Life`. 예 `FX/Hit/P_Hit_Slash`, `FX/AoE/P_AoE_Smash`.
- 몬스터(Rampage 뼈대) 전용: `Content/Character/Rampage/FX/` (원본 이름 유지).
- Duplicate는 참조가 원본 팩의 머티리얼·메시에 남습니다. 의존 에셋 이동은 "Paragon 에셋 최종 Move"([ROADMAP.md](ROADMAP.md)) 때 `jm_kit.refs`로 사슬을 뽑아 한 번에.

## 3. 제외 규칙 (120개)

| 제외 | 개수 | 이유 |
|---|---|---|
| `PS_*Camera`, `P_CameraEffect_*`, `P_*_CameraEffect`, `P_ReleaseArrow`, `P_ScreenBlast`, `P_GoldEnergy` | 14 | Camera. 피격 화면 효과는 PostProcess/UMG로 |
| `Sparrow/Effects/*Poster*`, `spp020~110` | 17 | 포스터 촬영용(`M_DOF_Test*`, `M_Sparrow_DOF_TestMartin`) |
| `*_Local`, `*_TEMP`, `P_PROTO_Proto_*`, `P_RogueSparrowTest`, `*_Homescreen`, `P_Rampage_Lunge_Impact`(구버전) | 11 | 1인칭 로컬 변형·임시·프로토·로비용·구버전. 같은 역할의 완성판이 따로 있음 |
| `*_WaterImpact`, `P_Rampage_Ice_Lunge_Water`, `P_Greystone_Novaborn_LeapAOE_Constellations_Water` | 13 | Water → 5장 보류 |
| Novaborn 스킨(별자리·DNA·드래곤 메시, SF 톤) — 빛기둥 2개 제외 전부 | 18 | `SM_Greystone_Novaborn_Constellations`·`DragonPiece` 등 SF 전용 메시, 중세 판타지 컨셉과 충돌 |
| Rogue 스킨 — 2개 제외 전부 | 15 | 기본 Sparrow 세트의 보라 리컬러 |
| IceBlue 스킨 — 8개 제외 전부 | 23 | 기본 Rampage 세트의 냉기 리컬러 |
| `P_Greystone_Statue_Destroy`, Novaborn `*Statue*`, `P_Greystone_HToKill_Trail*`(4), `P_HToKill_*DHead*` | 7 | Greystone 궁극기 전용 석상 파편 17종·용머리 메시 |
| `P_Rampage_Rock_Cancel`, `P_Rampage_Ice_Rock_Cancel` | 2 | 스킬 취소 연출 — 취소 패킷 없음 |

## 4. 선별 목록 (89개 복제: 공용 A~G 82 + 몬스터 전용 H 7)

원본 경로는 `/Game/` 아래 상대 경로(`…`는 앞 행과 같은 상위 폴더). 상태 열: `후보`(복제 전) → `복제`(2단계 완료) → `채택`/`탈락`(미리보기 시트 확인 후) → `손질 완료`.

### A. 무기 궤적 (Trail) — 몽타주 `AnimNotifyState_Trail`

| 원본 | 역할 | 근거 | 손볼 것 | 새 이름 | 상태 |
|---|---|---|---|---|---|
| `ParagonGreystone/FX/Particles/Greystone/Abilities/Primary/FX/P_Greystone_Primary_Trails_L` | **한손검 기본 공격 궤적**(연기 섞인 주황) | TRAIL+COLL/EVT, `M_Greystone_Primary_Trails_04`+`M_Greystone_Lunge_Smoke_INST`, bb 160 | `_R`은 좌우 미러 → 하나만 | `Trail/P_Trail_Sword_Basic` | 복제 |
| `…/Primary/FX/P_Greystone_Trail_OnlyWind` | **모든 무기 공용 약한 바람 궤적**(맨손·저강도) | TRAIL, 머티리얼 1개, 19KB 최경량 | — | `Trail/P_Trail_Wind` | 복제 |
| `…/LeapAOE/FX/P_Greystone_Leap_AOE_Trail` | **검 스킬 궤적**(밝은 플라즈마) | TRAIL, `M_Greystone_Primary_Trails_02` | `_GlowEdge`는 Greystone 칼날 메시 포함 → 제외 | `Trail/P_Trail_Sword_Skill` | 복제 |
| `…/ClearAPath/FX/P_Greystone_ClearAPath_Trail` | **회전·연타 스킬 궤적**(끝 부분 밝음) | TRAIL+BONESOCK(`FX_Sword_Top`) | 소켓명 → `FX_Weapon_Top` | `Trail/P_Trail_Sword_Spin` | 복제 |
| `…/Primary/FX/P_Greystone_Primary_Trails_Taunt` | **짧은 휘두르기 궤적**(감정표현·점검, 충돌 없음) | TRAIL만, 30KB | — | `Trail/P_Trail_Sword_Short` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/Primary/FX/P_MeleeTrails_Regular` | **맨손·둔기·대형 무기 궤적**(굴절 기반, 무채색) | TRAIL, `M_AnimTrail_Base`(`RefracPow`) | — | `Trail/P_Trail_Blunt` | 복제 |
| `…/Primary/FX/P_MeleeTrails_Enraged` | **버프 상태 궤적**(+플라즈마·잉크 연기) | TRAIL, +`M_Animtrail_Plasma`, `InkySmoke` | — | `Trail/P_Trail_Blunt_Buffed` | 복제 |
| `…/Primary/FX/P_MeleeTrails_RoarBuffed` | **광폭 버프 궤적**(폭 테셀레이션 다름) | TRAIL, `WidthTessellationStepSize` | Enraged와 비교 후 하나 | `Trail/P_Trail_Blunt_Roar` | 복제 |
| `ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_MeleeTrails_Regular` | **냉기 궤적** | TRAIL, `M_Rampage_Ice_AnimTrail_Base` | — | `Trail/P_Trail_Ice` | 복제 |

### B. 타격 (Hit) — `SC_DAMAGE` 수신 시 피격자 위치에 1회

| 원본 | 역할 | 근거 | 손볼 것 | 새 이름 | 상태 |
|---|---|---|---|---|---|
| `ParagonGreystone/FX/Particles/Greystone/Abilities/Primary/FX/P_Greystone_Primary_Impact` | **검 베기 타격**(불꽃+연기 터짐+방사 블러) — 한손검 평타 피격 | `M_Greystone_Sparks`·`M_Greystone_Smoke_Burst`·`M_Radial_Blur_Refraction`·`SM_Spline_X_Aligned`, bb 200 | — | `Hit/P_Hit_Slash` | 복제 |
| `…/ClearAPath/FX/P_Greystone_ClearAPath_Impact` | **검 강타격**(+직선 충격파 메시) — 스킬 피격 | +`SM_Verdict_Straight_Shockwave_01`·`M_Verdict_dust_burst_01` | — | `Hit/P_Hit_Slash_Heavy` | 복제 |
| `…/ClearAPath/FX/P_Greystone_ClearAPath_Impacts` | **다단 히트 경량 베기**(잉크 터짐, 연기 없음) | `InkyBurst`, 135KB, 블러 없음 | — | `Hit/P_Hit_Slash_Light` | 복제 |
| `…/Primary/FX/P_Greystone_Primary_Sparks` | **칼끝 불꽃**(막힘·갑옷 표현) | BONESOCK `sword_topsocket`, `M_Elec_Burst`, `SM_GreystoneEdge_01` | 소켓명 → `FX_Weapon_Top`, 엣지 메시 이미터 삭제 | `Hit/P_Hit_Spark_Tip` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/Primary/FX/P_Rampage_Melee_Impact` | **둔기·맨손 타격**(섬광+광선+GPU 스파크) — 맨손 1001 피격 | `M_Flowerburst_Flare_Master`·`M_Rampage_Lightbeams`·`M_Impact_Refraction`, bb 30 | — | `Hit/P_Hit_Blunt` | 복제 |
| `…/Primary/FX/P_Rampage_Enraged_Impact` | **치명타/강화 타격**(+강철 굴절·불꽃 메시) | +`M_SteelImpactRefraction`·`SM_Dive_Flames`, bb 55 | — | `Hit/P_Hit_Blunt_Crit` | 복제 |
| `ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Melee_Impact` | **냉기 타격** | `M_Rampage_Ice_Lightbeams` | — | `Hit/P_Hit_Ice` | 복제 |
| `ParagonSparrow/FX/Particles/Sparrow/Abilities/Primary/FX/P_Sparrow_HitHero` | **화살 명중(캐릭터)** — 활 3001 피격 | `SparkTrailHeads`, `M_Sparrow_Primary_Flare`·`M_Plasma_v5_Flipbook2`, bb 500 | 스케일 0.5 검토 | `Hit/P_Hit_Arrow` | 복제 |
| `…/Primary/FX/P_Sparrow_Primary_Ballistic_HitPlayer` | **화살 명중 경량**(다중 화살·장판 틱) | 머티리얼 4개, bb 100 | — | `Hit/P_Hit_Arrow_Light` | 복제 |
| `…/Primary/FX/P_Sparrow_Primary_Ballistic_HitWorld` | **화살 지면 박힘**(빗나감·투사체 지면 충돌) | `SM_Sparrow_Arrow`+불꽃+돔 | 화살 메시 → 우리 활 세트 화살 | `Hit/P_Hit_Arrow_Ground` | 복제 |
| `…/Ultimate/FX/P_Sparrow_UltHit` | **강화 화살 명중**(광선+불꽃 메시) — 차지샷·치명타 | `M_Shield_Lightbeams5`·`SM_Dive_Flames`, bb 240 | — | `Hit/P_Hit_Arrow_Heavy` | 복제 |
| `…/Ultimate/FX/P_Sparrow_UltHitWorld` | **강화 화살 지면 명중** | +`SM_Sparrow_Arrow`·`M_ArrowRain` | 화살 메시 교체 | `Hit/P_Hit_Arrow_Heavy_Ground` | 복제 |
| `ParagonSparrow/FX/Particles/Sparrow/Skins/Rogue/P_Sparrow_UltHit` | **암흑 명중**(그림자 덩굴) | +`M_RogueSparrow_Tendrils`·`M_ShadowSwirls_v1_Inst` | — | `Hit/P_Hit_Shadow` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitCharacter` | **투석·낙석 명중(캐릭터)**(바위 파편) | COLL/EVT, `SM_Rampage_Rock_Frag A~E`, bb 1200 | — | `Hit/P_Hit_Rock` | 복제 |

### C. 투사체 (Proj) — 투사체 액터에 부착

| 원본 | 역할 | 근거 | 손볼 것 | 새 이름 | 상태 |
|---|---|---|---|---|---|
| `ParagonSparrow/FX/Particles/Sparrow/Abilities/Primary/FX/P_Sparrow_PrimaryAttack` | **활 기본 화살**(1.6m 리본+섬광) — 3001 | RIBBON, bb 1600×200 | — | `Proj/P_Proj_Arrow` | 복제 |
| `…/Ultimate/FX/P_Arrow_Ultimate` | **관통/강사 화살**(2.5m, 코로나 메시) | RIBBON, `MeshCorona`, bb 2550 | — | `Proj/P_Proj_Arrow_Pierce` | 복제 |
| `…/Ultimate/FX/P_Sparrow_UltArrow` | **차지 화살**(오라+불꽃 메시) | RIBBON+`SM_Dive_Flames`·`SM_Aura_v1`·`M_Arrow_Aura4` | — | `Proj/P_Proj_Arrow_Charged` | 복제 |
| `…/DrawABead/FX/P_Sparrow_RMB` | **다발/산탄 화살**(펠릿, 떨어져 남는 화살) | RIBBON, `M_Pellets_InvisMoving`·`FallingLingerArrows`·`SparksStick`, bb 1590 | — | `Proj/P_Proj_Arrow_Multi` | 복제 |
| `…/RainOfArrows/FX/P_RainofArrowsProj` | **낙하 화살 단발**(소형) | RIBBON, bb 28 | — | `Proj/P_Proj_Arrow_Small` | 복제 |
| `ParagonSparrow/FX/Particles/Sparrow/Skins/Rogue/P_Sparrow_PrimaryRogue` | **암흑 화살**(그림자 소용돌이+날개 오라) | RIBBON, `M_ShadowSwirls_BrightColors`·`WingAuraR` | — | `Proj/P_Proj_Arrow_Shadow` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_Flying` | **투석 비행**(연기 꼬리) — 몬스터 투척·투척 무기 | `lingeringsmoke`, bb 300 | 바위 메시는 투사체 액터 쪽 | `Proj/P_Proj_Rock` | 복제 |
| `…/RipNToss/FX/P_Rampage_Rock_Idle` | **투석 준비(손에 든 상태)** 루프 | `dragsmoke`·`lingeringsmoke`, bb 600 | — | `Proj/P_Proj_Rock_Hold` | 복제 |
| `…/RipNToss/FX/P_Rampge_Rock_Throw` | **투석 발사 순간** | DELAY, `M_TransDebris_NoEmissive_Simple` | 원본 오타 → 새 이름으로 교정 | `Proj/P_Proj_Rock_Throw` | 복제 |

### D. 시전·발사·이동 (Cast) — 몽타주 노티파이 또는 `SC_ATTACK` 수신 시 시전자 위치

| 원본 | 역할 | 근거 | 손볼 것 | 새 이름 | 상태 |
|---|---|---|---|---|---|
| `ParagonSparrow/FX/Particles/Sparrow/Abilities/Primary/FX/P_SparrowPrimaryMuzzleFlash` | **활 발사 섬광** | `MuzzleFlare`·`MuzzleGlow`, bb 100 | — | `Cast/P_Cast_Bow_Release` | 복제 |
| `…/DrawABead/FX/P_PROTO_ReadyState` | **조준·차지 중 글로우**(`AimMaxMs` 조준 루프) | `MI_Proto_ChargeUp`, bb 160 | 유일한 PROTO 생존(대체물 없음) | `Cast/P_Cast_Charge` | 복제 |
| `ParagonGreystone/FX/Particles/Greystone/Abilities/Primary/FX/P_Greystone_Spawn_DrawSword` | **무기 뽑기/장착 변경**(`SC_CHANGE_EQUIP`) | `Start_Burst`+`M_ArrowFlare`, bb 200, 62KB | — | `Cast/P_Cast_Equip` | 복제 |
| `…/LeapAOE/FX/P_Greystone_LeapAOE_Start` | **도약·돌진 시작**(회전 바닥+방향 충격파+먼지) | `SM_Greystone_WhirlBase_01`·`M_Verdict_Directional_Shockwave01`, bb 600 | — | `Cast/P_Cast_Leap` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/Lunge/FX/P_Rampage_Lunge_Jumpdust` | **점프/대시 발 먼지**(돌 조각) | `VF_Swirl_Cone_36`, `HighRocks/LowRocks`, bb 500 | `P_Rampage_Jumpdust`(연기만, 69KB)와 비교 후 하나 | `Cast/P_Cast_Dash_Dust` | 복제 |
| `ParagonSparrow/FX/Particles/Sparrow/Effects/P_Sparrow_DustKickUp` | **구르기/착지/넉백 먼지**(경량) | `M_Knockback_Dust`, 66KB, bb 300 | — | `Cast/P_Move_Dust_Light` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/Ultimate/FX/P_Enrage_Cast` | **버프 시전(화염·열파)** — 광폭화 | `M_ReverseDepthFire`·`Heatwave`·`M_Shockwave_Add_Refract`, bb 900 | — | `Cast/P_Cast_Buff_Fire` | 복제 |
| `ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Enrage_Cast` | **냉기 폭발 시전**(얼음 가시+눈송이) — 프로스트 노바 | `IceSpikes`·`SM_Ice_Fragment`·`M_Rampage_Ice_Spike_Spline`, bb 900 | BONESOCK 이미터(Rampage 본) 삭제 | `Cast/P_Cast_Buff_Ice` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/Roar/FX/P_Rampage_Roar_Radius` | **포효/도발/함성 광역**(원뿔 공기파+왜곡 돔) | `M_AirWaves_Cone`·`M_RoarDistortion`·`SM_Rampage_SlowDome`, `Param_Rampage_Roarsize` | 입 침 이미터(`Liquid Mouth Drips`·`M_RampageSpit_Drops`) 삭제 | `Cast/P_Cast_Shout` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_RipNToss_Rip` | **지면 찢기/대지 가시 시전**(크레이터+파편 상승) | COLL, `SM_Rampage_Rock_Rip_Crater`, bb 170 | — | `Cast/P_Cast_Earth_Rip` | 복제 |
| `…/RipNToss/FX/P_RipNToss_HandImpact` | **주먹 내려찍기 소형** — 맨손 스킬 | `FastRound`·`FastSmoke`, bb 90 | — | `Cast/P_Cast_Fist_Slam` | 복제 |
| `ParagonGreystone/FX/Particles/Greystone/Abilities/Ultimate/FX/P_Greystone_HToKill_WarmUp` | **대형 스킬 채널링**(불씨+바위 소용돌이 상승) | `WarmUp_Embers/Rock/tendril`, bb 2000 | 스케일 0.5 검토 | `Cast/P_Cast_Channel_Big` | 복제 |

### E. 광역·지면 (AoE) — `SkillTable.Fx`, `FxBaseRadius` = bb/2

| 원본 | 역할 | 근거 | 손볼 것 | 새 이름 | 상태 |
|---|---|---|---|---|---|
| `ParagonGreystone/FX/Particles/Greystone/Abilities/LeapAOE/FX/P_Greystone_LeapAOE_GroundSmash` | **(사용 중) 검 지면 강타 원형** 2002 | COLL/EVT, bb 1300 | Water 이미터(`FoamRipple`·`PM_ShallowWater`) 분리 검토 | `AoE/P_AoE_Smash` (기존 `Character/Greystone/P_Greystone_GroundSmash` 이동) | 이동 완료(사용 중) |
| `…/LeapAOE/FX/P_LeapSmash_Location` | **스킬 조준 텔레그래프(바닥 원)** — 디버그 원 대체 | `M_Radius_Indicator`·`SM_Greystone_Aoe_Mesh_Sweep`, bb 1700 | 거품·불꽃 이미터 삭제, 반경 파라미터화 | `AoE/P_AoE_Telegraph` | 복제 |
| `ParagonSparrow/FX/Particles/Sparrow/Abilities/RainOfArrows/FX/P_RainofArrows` | **(사용 중) 화살 비 장판** 3002 | COLL/EVT+RIBBON, bb 1000×4250 | Water 이미터 분리 검토 | `AoE/P_AoE_ArrowRain` (기존 `Character/Sparrow/P_Sparrow_RainofArrows` 이동) | 이동 완료(사용 중) |
| `…/RainOfArrows/FX/P_RainArrowsTarget` | **장판 채널 중 시전자 표시**(활 불꽃 리본) | BONESOCK+RIBBON, `M_FA_Glow_Fury`, bb 400 | 본 → 우리 활 소켓(`hand_l`) | `AoE/P_AoE_ArrowRain_Caster` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/Lunge/FX/P_Rampage_Lunge_ImpactNEW` | **대형 지면 충격(화염 충격파+바위)** — 양손 무기·몬스터 | COLL/EVT, `FlameShockwave`·`SM_Rampage_UltSmashShockwave`, bb 800 | — | `AoE/P_AoE_Slam_Fire` | 복제 |
| `…/Lunge/FX/P_Rampage_Lunge_Impact_Enraged` | **대형 지면 충격(바위·연기만)** — 대지 | `M_Shockwave_Core`·`SM_Boulders_1`, bb 600 | 구버전 `Impact`와 거의 같아 이것만 | `AoE/P_AoE_Slam_Earth` | 복제 |
| `ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Lunge_Impact` | **냉기 지면 충격** | `Flakes`, `M_Rampage_Ice_*`, bb 600 | — | `AoE/P_AoE_Slam_Ice` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld` | **낙석/투석 지면 착탄**(파편 5종) | COLL/EVT, `SM_Rampage_Rock_Frag A~E`, bb 1300 | — | `AoE/P_AoE_Rock_Land` | 복제 |
| `ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Rock_HitWorld` | **얼음 가시 지대**(착탄점에서 가시가 자람) — 냉기 장판 | `M_Rampage_Ice_Spike_Spline`·`SM_Forward_Burst_Splines`·`Grow`·`Spikes` | — | `AoE/P_AoE_Ice_Spikes` | 복제 |
| `ParagonSparrow/FX/Particles/Sparrow/Abilities/Ultimate/FX/P_Sparrow_Burst` | **폭발/화염 파열 원형**(링 충격파+불꽃) | `M_CoreAOE_Flat_Shockwave`·`SM_ShockwaveFlame`, bb 440 | — | `AoE/P_AoE_Burst_Fire` | 복제 |
| `ParagonGreystone/FX/Particles/Greystone/Abilities/ClearAPath/FX/P_Greystone_SwordSpinning_Start` | **회전 베기 시작**(지면 베인 자국+스와이프+돌) | `SM_ClearAPath_GroundCut_Foils`·`M_Greystone_Spira_GroundCut`, bb 2000×270 | 스와이프 메시 형태 확인 | `AoE/P_AoE_Spin_Start` | 복제 |
| `…/ClearAPath/FX/P_Greystone_SwordSpinning_01` | **회전 베기 지속**(캐릭터 부착, 소용돌이+나선선) | `Whirlwind`·`Spiral_Lines`·`VF_Galaxyspin`, bb 1200×400 | — | `AoE/P_AoE_Spin_Loop` | 복제 |
| `…/ClearAPath/FX/P_Greystone_SwordSpinning_End` | **회전 베기 종료 연기** | `EndSmoke`, 38KB | — | `AoE/P_AoE_Spin_End` | 복제 |
| `…/ClearAPath/FX/P_Greystone_ClearAPath_Swipe` | **참격 파동(앞으로 날리는 베기 자국)** — 직선 스킬 | `SM_Greystone_ClearAPath_StartSwipe`, bb 800×500 | 방향 = `AimYaw` | `AoE/P_AoE_Slash_Wave` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/Lunge/FX/P_Rampage_SmashArc` | **발톱/도끼 휘두름 호(弧)** — 둔기·양손 평타 | `SM_Rampage_Leap_Swipe`, bb 300×400 | `_Enraged`는 크기만 → 스케일로 | `AoE/P_AoE_Swipe_Arc` | 복제 |
| `ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_SmashArc` | **냉기 휘두름 호** | `M_SmashArc`·`SM_Shape_Rampage_SmashArc` | — | `AoE/P_AoE_Swipe_Arc_Ice` | 복제 |
| `ParagonGreystone/FX/Particles/Greystone/Abilities/Ultimate/FX/P_Greystone_Ultimate_Slow` | **둔화/속박 장판**(토러스+회전 도형+느린 충격파) | `M_Torus_Glow`·`M_Slow_Spline_V02/03`·`SM_DeathSpiral`, bb 2200 | 스케일 0.3~0.5 | `AoE/P_AoE_Slow_Field` | 복제 |

### F. 지속 상태 (Buff) — 캐릭터 부착 루프, 종료 시 1회

| 원본 | 역할 | 근거 | 손볼 것 | 새 이름 | 상태 |
|---|---|---|---|---|---|
| `ParagonRampage/FX/Particles/Abilities/Ultimate/FX/P_Enrage_Looping` | **분노/공격력 버프 오라**(전기 호+스파크 궤도) | `M_BuffArcs`·`SM_BlobbySphere`, bb 300 | — | `Buff/P_Buff_Rage_Loop` | 복제 |
| `…/Ultimate/FX/P_Enrage_End` | **버프 종료 터짐** | `BrightFlare`·`M_Shockwave_Add_Refract`, bb 900 | 스케일 0.5 | `Buff/P_Buff_Rage_End` | 복제 |
| `ParagonRampage/FX/Particles/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Enrage_Looping` | **냉기 버프 오라** | 얼음 변형 | — | `Buff/P_Buff_Ice_Loop` | 복제 |
| `…/Rampage_v001_IceBlue/FX/P_Rampage_Ice_Enrage_End` | **냉기 버프 종료** | 얼음 변형 | — | `Buff/P_Buff_Ice_End` | 복제 |
| `ParagonGreystone/FX/Particles/Greystone/Abilities/Deflect/FX/P_Greystone_Deflect_Looping` | **방어막/막기 상태**(안개 구+스파크) — 방패 막기·보호막 | `MI_Fog_SphericalDensity_Particle`·`SM_Sphere_FlippedNormals`, bb 160 | — | `Buff/P_Buff_Shield_Loop` | 복제 |
| `…/Deflect/FX/P_Greystone_Deflect_Remove` | **방어막 파괴/해제**(돔 깨짐+불꽃+크로마) | +`SM_Greystone_SlowDome`·`M_BombFlames`, bb 400 | — | `Buff/P_Buff_Shield_Break` | 복제 |
| `ParagonSparrow/FX/Particles/Sparrow/Abilities/Ultimate/FX/P_SparrowBuff` | **무기 인챈트(활 불꽃)** | BONESOCK+RIBBON, `SM_Sparrow_Bow_Flames`, bb 400 | 활 본 → 우리 활 소켓, 불꽃 메시 스케일 | `Buff/P_Buff_Weapon_Fire_Bow` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/JungleKing/FX/P_Rampage_E_embers` | **불씨 오라(몸에서 재)** — 화상 디버프 | BONESOCK, `M_SoulEmbers`, bb 180 | 본 → 매니퀸 `spine_03` 등 | `Buff/P_Debuff_Burn` | 복제 |
| `ParagonGreystone/FX/Particles/Greystone/Abilities/Ultimate/FX/P_Greystone_Ultimate_Glow_Light` | **눈 빛남 궤적** — 보스·광폭화·각성 | BONESOCK+RIBBON `Light_L/R`, `M_Greystone_EyeTrail_Inst` | 본 → `head` 오프셋 | `Buff/P_Buff_EyeGlow` | 복제 |
| `…/Ultimate/FX/P_Greystone_Ultimate_Glow_Activate` | **눈 빛남 시작 연기** | `Smoke_Puff`, bb 140 | — | `Buff/P_Buff_EyeGlow_Start` | 복제 |
| `…/Ultimate/FX/P_HToKill_TurnToStone` | **석화 디버프**(바위판 덮임→부서짐) | BONESOCK+COLL/EVT, `M_RockSlab`·`SM_Boulders_1`, bb 300 | 본 이름 교체 | `Buff/P_Debuff_Petrify` | 복제 |
| `…/Ultimate/FX/P_Greystone_Ultimate_Stone` | **석화/스톤스킨 지속**(어깨·손 연기) | BONESOCK `shoulder_l/r`·`pinky_l`, `M_ImposterRock_Translucent_MASTER` | 매니퀸 `clavicle_l/r`·`pinky_01_l` 확인 | `Buff/P_Debuff_Petrify_Loop` | 복제 |
| `…/Ultimate/FX/P_Greystone_HToKill_After` | **잔불/여운**(불씨만) | `VF_TurbulentNoise_1_55`+`M_Greystone_Sparks`, bb 1200 | 스케일 0.5 | `Buff/P_Aura_Embers_Linger` | 복제 |

### G. 생애주기 (Life) — 스폰·사망·부활·레벨업·귀환

| 원본 | 역할 | 근거 | 손볼 것 | 새 이름 | 상태 |
|---|---|---|---|---|---|
| `ParagonGreystone/FX/Particles/Greystone/Skins/Novaborn/P_Greystone_Novaborn_LevelStart_ShieldSpawn` | **레벨업 빛기둥**(위로 솟는 광선, 4m) — A2.3 | `M_UpEmbers_LightShafts`, bb 400×400×900 | 별자리 메시 이미터 삭제 → 광선만 | `Life/P_Life_LevelUp` | 복제 |
| `…/Skins/Novaborn/P_Greystone_Novaborn_Spawn_Light` | **스폰/부활 지점 빛**(최경량) — `SC_ADD_OBJECT` | 외부 참조 0, 57KB, bb 400 | — | `Life/P_Life_Spawn` | 복제 |
| `ParagonGreystone/FX/Particles/Greystone/Abilities/Ultimate/FX/P_Greystone_HToKill_Start` | **부활/대형 시전 빔**(몸에서 하늘로) | BONESOCK `BigBeam_Body/Sword`, `M_Greystone_Ultimate_PlasmaBeam_Up`, bb 1200 | 본 → `spine_03`, `Sword` 이미터 삭제 | `Life/P_Life_Beam_Up` | 복제 |
| `…/Ultimate/FX/P_Greystone_HToKill_Resurrect` | **부활 폭발**(바람 러시+워프 구) — A2.4 | `M_LeapAOE_Projectile_Wind_Rush`·`SM_Impact_Warp`, bb 1000 | 얼굴 본 이미터 전부 삭제 | `Life/P_Life_Resurrect` | 복제 |
| `ParagonSparrow/FX/Particles/Sparrow/Effects/P_Death_Sparrow` | **사망 소멸**(몸에서 잉크 연기+재) — A2.4·몬스터 | BONESOCK(Sparrow 본 다수), `M_Smoke_Wisp_Ink_SubUV`·`M_hardSpark_Death_Edge`, bb 200 | 본 목록 → 매니퀸 주요 본 | `Life/P_Life_Death` | 복제 |
| `…/Effects/P_Recall_BoneSocket_Particles_Sparrow` | **귀환/텔레포트 채널링**(몸 빛점) | BONESOCK, `M_Recall_GlowingDots_Inst`, bb 300 | 본 재지정 | `Life/P_Life_Recall` | 복제 |
| `ParagonGreystone/FX/Particles/Greystone/Abilities/Primary/FX/P_Greystone_Recall_Trails` | **귀환 완료/순간이동 궤적** | TRAIL+COLL/EVT, 81KB | 무기 소켓 | `Life/P_Life_Recall_Trail` | 복제 |
| `ParagonRampage/FX/Particles/Abilities/Ultimate/FX/P_Rampage_Footstep_Enraged` | **대형 몬스터 발자국**(돌 튐+연기) | `Rocks_Kick_Forward/Backward`, bb 700 | — | `Life/P_Env_Footstep_Heavy` | 복제 |

### H. 몬스터 전용 유지 → `Content/Character/Rampage/FX/` (원본 이름)

| 원본 | 역할 | 근거 |
|---|---|---|
| `ParagonRampage/FX/Particles/Abilities/JungleKing/FX/P_Rampage_Passive_Attack` | 손가락 발광+바닥 데칼(강화 공격) | BONESOCK `Hand L/R Index·Pinky·Thumb` |
| `…/RipNToss/FX/P_Rampage_Rock_Idle` → `P_Rampge_Rock_Throw` → `P_Rampage_Rock_Flying` → `P_Rampage_Rock_HitWorld` / `P_Rampage_Rock_HitCharacter` | **투석 스킬 한 세트**(몬스터 2번 스킬 후보) | B·C·E의 공용 사본과 별개로 몬스터용 원본 유지 |
| `…/Roar/FX/P_Rampage_Roar_Radius` | 몬스터 포효(입 침 이미터 유지) | D의 플레이어용 사본과 별개 |

## 5. 보류 (13개, 물 지형 생기면)

`*_WaterImpact` 계열 — Greystone 1(`P_Greystone_Novaborn_LeapAOE_Constellations_Water`; `P_Greystone_LeapAOE_GroundSmash` 안의 물 이미터는 분리), Rampage 7(`P_Rampage_Lunge_Impact_WaterImpact`, `P_Rampage_Lunge_Impact_Enraged_WaterImpact`, `P_Rampage_Rock_HitWorld_WaterImpact`, `P_RipNToss_Rip_WaterImpact`, `P_RipNToss_WaterImpact`, IceBlue `P_Rampage_Ice_Lunge_Water`·`P_Rampage_Ice_Lunge_Impact_Enraged_Water`), IceBlue 2(`P_Rampage_Ice_RipNToss_Rip_WaterImpact`, `P_Rampage_Ice_Rock_HitWorld_WaterImpact`), Sparrow 3(`P_Sparrow_Primary_Ballistic_HitWorld_WaterImpact`, `P_Sparrow_UltHitWorld_WaterImpact`, Rogue `P_Sparrow_Primary_Ballistic_HitWorld_WaterImpact`).

## 6. 실행 단계와 상태

- [x] 1. 이 카탈로그 작성, `DESIGN_ART_CONCEPT.md` 6장 링크 (2026-11-10)
- [x] 2. 복제 (2026-11-10): MCP `AssetTools.duplicate` 87개 + `move` 2개(`P_AoE_Smash`·`P_AoE_ArrowRain`, 리다이렉터 없이 이동됨) → `/Game/FX` 82 + `Character/Rampage/FX` 7, 의존은 전부 엔진·Paragon만, `SkillTable.csv` `Fx` 갱신 + `DT_SkillTable` 재임포트(6행)
- [x] 3a. 미리보기 시트 도구·촬영 (2026-11-10): `jm_kit.fx_sheet` + MCP `fx_sheet_start/status/stop`(에디터 재시작으로 등록), 82개 × 0.3·1.0·2.0초 → `Saved/MCShots/fx_sheet_all_<분류>.png`(행 = 에셋, 열 = 시점, 행 순서 `fx_sheet_index.md`) + 낱장 `fx_<이름>_t030/t100/t200.png`. 시트에서 **비어 보이는 것은 도구 한계**이지 탈락 근거가 아님 — Trail 9개(AnimTrail은 몽타주 노티파이·소켓 필요), Proj 대부분(리본은 이동해야 그려짐), BONESOCK 계열(Paragon 본), 일부 Buff(인스턴스 파라미터로 켜지는 이미터 — 4단계에서 기본값 확인). 도구 함정은 `UEClient/JM/CLAUDE.md` jm_kit.fx_sheet 참고
- [ ] 3b. 사용자 채택/탈락 결정 → 상태 열 갱신(`복제` → `채택`/`탈락`), `TESTING.md`에 시트 경로·날짜 기록
- [ ] 4. 손질(채택분): 본 이름 교체·삭제(F·G), 화살 메시 교체(B), 침·별자리·거품 삭제(D·E·G), Water 분리(E), 매니퀸 스켈레톤 `FX_Weapon_Top/Bottom` 소켓(A 선행)
- [ ] 5. 문서: `DECISIONS.md` 1행(선별 기준·제외 규칙·네이밍 `P_<Role>_<Name>`), `ROADMAP.md` MC4.5, `ExternalAssets.md` 비고

검증: 2단계 뒤 `/Game/FX` ParticleSystem 수 ≈ 73, 의존은 엔진·Paragon만, PIE에서 2002·3002 재생. 3단계 시트는 Read로 크기·색 확인, 승인은 `TESTING.md`. 4단계 BONESOCK 계열이 매니퀸에서 빈 이미터 없이, TRAIL이 `AM_OneHandSword_Attack`에서 칼 길이만큼.
