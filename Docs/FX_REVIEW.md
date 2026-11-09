# FX 라이브러리 검토 (2026-11-10 시트)

선별 근거와 전체 목록은 [FX_CATALOG.md](FX_CATALOG.md). 이 문서는 **채택/탈락 결정용** — 시트(행 = 에셋, 열 = 0.3 / 1.0 / 2.0초)를 보고 `판정` 열에 채택·탈락·보류를 적습니다. 시트와 낱장은 `UEClient/JM/Saved/MCShots/`(git 제외)에 있고, 촬영 방법·함정은 `UEClient/JM/CLAUDE.md`의 `jm_kit.fx_sheet`.

**시트에서 비어 보여도 탈락 근거가 아닌 경우**(도구 한계, `비고` 열에 표시): Trail = AnimTrail이라 몽타주 노티파이·무기 소켓이 있어야 그려짐 / Proj = 리본 궤적은 투사체가 움직여야 보임 / 본 = Paragon 본 이름에 묶인 이미터(4단계에서 매니퀸 본으로 교체) / 파라미터 = 인스턴스 파라미터로 켜지는 이미터로 보임(4단계에서 기본값 확인).

## Trail — `Saved/MCShots/fx_sheet_all_Trail.png`

![Trail](../UEClient/JM/Saved/MCShots/fx_sheet_all_Trail.png)

| 행 | 에셋 | 역할 | 손볼 것 | 비고 | 판정 |
|---|---|---|---|---|---|
| 1 | `P_Trail_Blunt` | **맨손·둔기·대형 무기 궤적**(굴절 기반, 무채색) | — | Trail | |
| 2 | `P_Trail_Blunt_Buffed` | **버프 상태 궤적**(+플라즈마·잉크 연기) | — | Trail | |
| 3 | `P_Trail_Blunt_Roar` | **광폭 버프 궤적**(폭 테셀레이션 다름) | Enraged와 비교 후 하나 | Trail | |
| 4 | `P_Trail_Ice` | **냉기 궤적** | — | Trail | |
| 5 | `P_Trail_Sword_Basic` | **한손검 기본 공격 궤적**(연기 섞인 주황) | `_R`은 좌우 미러 → 하나만 | Trail | |
| 6 | `P_Trail_Sword_Short` | **짧은 휘두르기 궤적**(감정표현·점검, 충돌 없음) | — | Trail | |
| 7 | `P_Trail_Sword_Skill` | **검 스킬 궤적**(밝은 플라즈마) | `_GlowEdge`는 Greystone 칼날 메시 포함 → 제외 | Trail | |
| 8 | `P_Trail_Sword_Spin` | **회전·연타 스킬 궤적**(끝 부분 밝음) | 소켓명 → `FX_Weapon_Top` | Trail·본 | |
| 9 | `P_Trail_Wind` | **모든 무기 공용 약한 바람 궤적**(맨손·저강도) | — | Trail | |

## Hit — `Saved/MCShots/fx_sheet_all_Hit.png`

![Hit](../UEClient/JM/Saved/MCShots/fx_sheet_all_Hit.png)

| 행 | 에셋 | 역할 | 손볼 것 | 비고 | 판정 |
|---|---|---|---|---|---|
| 1 | `P_Hit_Arrow` | **화살 명중(캐릭터)** — 활 3001 피격 | 스케일 0.5 검토 |  | |
| 2 | `P_Hit_Arrow_Ground` | **화살 지면 박힘**(빗나감·투사체 지면 충돌) | 화살 메시 → 우리 활 세트 화살 |  | |
| 3 | `P_Hit_Arrow_Heavy` | **강화 화살 명중**(광선+불꽃 메시) — 차지샷·치명타 | — |  | |
| 4 | `P_Hit_Arrow_Heavy_Ground` | **강화 화살 지면 명중** | 화살 메시 교체 |  | |
| 5 | `P_Hit_Arrow_Light` | **화살 명중 경량**(다중 화살·장판 틱) | — |  | |
| 6 | `P_Hit_Blunt` | **둔기·맨손 타격**(섬광+광선+GPU 스파크) — 맨손 1001 피격 | — |  | |
| 7 | `P_Hit_Blunt_Crit` | **치명타/강화 타격**(+강철 굴절·불꽃 메시) | — |  | |
| 8 | `P_Hit_Ice` | **냉기 타격** | — |  | |
| 9 | `P_Hit_Rock` | **투석·낙석 명중(캐릭터)**(바위 파편) | — |  | |
| 10 | `P_Hit_Shadow` | **암흑 명중**(그림자 덩굴) | — |  | |
| 11 | `P_Hit_Slash` | **검 베기 타격**(불꽃+연기 터짐+방사 블러) — 한손검 평타 피격 | — |  | |
| 12 | `P_Hit_Slash_Heavy` | **검 강타격**(+직선 충격파 메시) — 스킬 피격 | — |  | |
| 13 | `P_Hit_Slash_Light` | **다단 히트 경량 베기**(잉크 터짐, 연기 없음) | — |  | |
| 14 | `P_Hit_Spark_Tip` | **칼끝 불꽃**(막힘·갑옷 표현) | 소켓명 → `FX_Weapon_Top`, 엣지 메시 이미터 삭제 | 본 | |

## Proj — `Saved/MCShots/fx_sheet_all_Proj.png`

![Proj](../UEClient/JM/Saved/MCShots/fx_sheet_all_Proj.png)

| 행 | 에셋 | 역할 | 손볼 것 | 비고 | 판정 |
|---|---|---|---|---|---|
| 1 | `P_Proj_Arrow` | **활 기본 화살**(1.6m 리본+섬광) — 3001 | — | Proj | |
| 2 | `P_Proj_Arrow_Charged` | **차지 화살**(오라+불꽃 메시) | — | Proj | |
| 3 | `P_Proj_Arrow_Multi` | **다발/산탄 화살**(펠릿, 떨어져 남는 화살) | — | Proj | |
| 4 | `P_Proj_Arrow_Pierce` | **관통/강사 화살**(2.5m, 코로나 메시) | — | Proj | |
| 5 | `P_Proj_Arrow_Shadow` | **암흑 화살**(그림자 소용돌이+날개 오라) | — | Proj | |
| 6 | `P_Proj_Arrow_Small` | **낙하 화살 단발**(소형) | — | Proj | |
| 7 | `P_Proj_Rock` | **투석 비행**(연기 꼬리) — 몬스터 투척·투척 무기 | 바위 메시는 투사체 액터 쪽 | Proj | |
| 8 | `P_Proj_Rock_Hold` | **투석 준비(손에 든 상태)** 루프 | — | Proj | |
| 9 | `P_Proj_Rock_Throw` | **투석 발사 순간** | 원본 오타 → 새 이름으로 교정 | Proj | |

## Cast — `Saved/MCShots/fx_sheet_all_Cast.png`

![Cast](../UEClient/JM/Saved/MCShots/fx_sheet_all_Cast.png)

| 행 | 에셋 | 역할 | 손볼 것 | 비고 | 판정 |
|---|---|---|---|---|---|
| 1 | `P_Cast_Bow_Release` | **활 발사 섬광** | — |  | |
| 2 | `P_Cast_Buff_Fire` | **버프 시전(화염·열파)** — 광폭화 | — |  | |
| 3 | `P_Cast_Buff_Ice` | **냉기 폭발 시전**(얼음 가시+눈송이) — 프로스트 노바 | BONESOCK 이미터(Rampage 본) 삭제 |  | |
| 4 | `P_Cast_Channel_Big` | **대형 스킬 채널링**(불씨+바위 소용돌이 상승) | 스케일 0.5 검토 |  | |
| 5 | `P_Cast_Charge` | **조준·차지 중 글로우**(`AimMaxMs` 조준 루프) | 유일한 PROTO 생존(대체물 없음) |  | |
| 6 | `P_Cast_Dash_Dust` | **점프/대시 발 먼지**(돌 조각) | `P_Rampage_Jumpdust`(연기만, 69KB)와 비교 후 하나 |  | |
| 7 | `P_Cast_Earth_Rip` | **지면 찢기/대지 가시 시전**(크레이터+파편 상승) | — |  | |
| 8 | `P_Cast_Equip` | **무기 뽑기/장착 변경**(`SC_CHANGE_EQUIP`) | — |  | |
| 9 | `P_Cast_Fist_Slam` | **주먹 내려찍기 소형** — 맨손 스킬 | — |  | |
| 10 | `P_Cast_Leap` | **도약·돌진 시작**(회전 바닥+방향 충격파+먼지) | — |  | |
| 11 | `P_Cast_Shout` | **포효/도발/함성 광역**(원뿔 공기파+왜곡 돔) | 입 침 이미터(`Liquid Mouth Drips`·`M_RampageSpit_Drops`) 삭제 |  | |
| 12 | `P_Move_Dust_Light` | **구르기/착지/넉백 먼지**(경량) | — |  | |

## AoE — `Saved/MCShots/fx_sheet_all_AoE.png`

![AoE](../UEClient/JM/Saved/MCShots/fx_sheet_all_AoE.png)

| 행 | 에셋 | 역할 | 손볼 것 | 비고 | 판정 |
|---|---|---|---|---|---|
| 1 | `P_AoE_ArrowRain` | **(사용 중) 화살 비 장판** 3002 | Water 이미터 분리 검토 |  | |
| 2 | `P_AoE_ArrowRain_Caster` | **장판 채널 중 시전자 표시**(활 불꽃 리본) | 본 → 우리 활 소켓(`hand_l`) | 본 | |
| 3 | `P_AoE_Burst_Fire` | **폭발/화염 파열 원형**(링 충격파+불꽃) | — |  | |
| 4 | `P_AoE_Ice_Spikes` | **얼음 가시 지대**(착탄점에서 가시가 자람) — 냉기 장판 | — |  | |
| 5 | `P_AoE_Rock_Land` | **낙석/투석 지면 착탄**(파편 5종) | — |  | |
| 6 | `P_AoE_Slam_Earth` | **대형 지면 충격(바위·연기만)** — 대지 | 구버전 `Impact`와 거의 같아 이것만 |  | |
| 7 | `P_AoE_Slam_Fire` | **대형 지면 충격(화염 충격파+바위)** — 양손 무기·몬스터 | — |  | |
| 8 | `P_AoE_Slam_Ice` | **냉기 지면 충격** | — |  | |
| 9 | `P_AoE_Slash_Wave` | **참격 파동(앞으로 날리는 베기 자국)** — 직선 스킬 | 방향 = `AimYaw` |  | |
| 10 | `P_AoE_Slow_Field` | **둔화/속박 장판**(토러스+회전 도형+느린 충격파) | 스케일 0.3~0.5 |  | |
| 11 | `P_AoE_Smash` | **(사용 중) 검 지면 강타 원형** 2002 | Water 이미터(`FoamRipple`·`PM_ShallowWater`) 분리 검토 |  | |
| 12 | `P_AoE_Spin_End` | **회전 베기 종료 연기** | — |  | |
| 13 | `P_AoE_Spin_Loop` | **회전 베기 지속**(캐릭터 부착, 소용돌이+나선선) | — |  | |
| 14 | `P_AoE_Spin_Start` | **회전 베기 시작**(지면 베인 자국+스와이프+돌) | 스와이프 메시 형태 확인 |  | |
| 15 | `P_AoE_Swipe_Arc` | **발톱/도끼 휘두름 호(弧)** — 둔기·양손 평타 | `_Enraged`는 크기만 → 스케일로 |  | |
| 16 | `P_AoE_Swipe_Arc_Ice` | **냉기 휘두름 호** | — |  | |
| 17 | `P_AoE_Telegraph` | **스킬 조준 텔레그래프(바닥 원)** — 디버그 원 대체 | 거품·불꽃 이미터 삭제, 반경 파라미터화 |  | |

## Buff — `Saved/MCShots/fx_sheet_all_Buff.png`

![Buff](../UEClient/JM/Saved/MCShots/fx_sheet_all_Buff.png)

| 행 | 에셋 | 역할 | 손볼 것 | 비고 | 판정 |
|---|---|---|---|---|---|
| 1 | `P_Aura_Embers_Linger` | **잔불/여운**(불씨만) | 스케일 0.5 |  | |
| 2 | `P_Buff_EyeGlow` | **눈 빛남 궤적** — 보스·광폭화·각성 | 본 → `head` 오프셋 | 본 | |
| 3 | `P_Buff_EyeGlow_Start` | **눈 빛남 시작 연기** | — |  | |
| 4 | `P_Buff_Ice_End` | **냉기 버프 종료** | — |  | |
| 5 | `P_Buff_Ice_Loop` | **냉기 버프 오라** | — | 파라미터 | |
| 6 | `P_Buff_Rage_End` | **버프 종료 터짐** | 스케일 0.5 |  | |
| 7 | `P_Buff_Rage_Loop` | **분노/공격력 버프 오라**(전기 호+스파크 궤도) | — | 파라미터 | |
| 8 | `P_Buff_Shield_Break` | **방어막 파괴/해제**(돔 깨짐+불꽃+크로마) | — | 파라미터 | |
| 9 | `P_Buff_Shield_Loop` | **방어막/막기 상태**(안개 구+스파크) — 방패 막기·보호막 | — | 파라미터 | |
| 10 | `P_Buff_Weapon_Fire_Bow` | **무기 인챈트(활 불꽃)** | 활 본 → 우리 활 소켓, 불꽃 메시 스케일 | 본 | |
| 11 | `P_Debuff_Burn` | **불씨 오라(몸에서 재)** — 화상 디버프 | 본 → 매니퀸 `spine_03` 등 | 본 | |
| 12 | `P_Debuff_Petrify` | **석화 디버프**(바위판 덮임→부서짐) | 본 이름 교체 | 본 | |
| 13 | `P_Debuff_Petrify_Loop` | **석화/스톤스킨 지속**(어깨·손 연기) | 매니퀸 `clavicle_l/r`·`pinky_01_l` 확인 | 본 | |

## Life — `Saved/MCShots/fx_sheet_all_Life.png`

![Life](../UEClient/JM/Saved/MCShots/fx_sheet_all_Life.png)

| 행 | 에셋 | 역할 | 손볼 것 | 비고 | 판정 |
|---|---|---|---|---|---|
| 1 | `P_Env_Footstep_Heavy` | **대형 몬스터 발자국**(돌 튐+연기) | — |  | |
| 2 | `P_Life_Beam_Up` | **부활/대형 시전 빔**(몸에서 하늘로) | 본 → `spine_03`, `Sword` 이미터 삭제 | 본 | |
| 3 | `P_Life_Death` | **사망 소멸**(몸에서 잉크 연기+재) — A2.4·몬스터 | 본 목록 → 매니퀸 주요 본 | 본 | |
| 4 | `P_Life_LevelUp` | **레벨업 빛기둥**(위로 솟는 광선, 4m) — A2.3 | 별자리 메시 이미터 삭제 → 광선만 |  | |
| 5 | `P_Life_Recall` | **귀환/텔레포트 채널링**(몸 빛점) | 본 재지정 | 본 | |
| 6 | `P_Life_Recall_Trail` | **귀환 완료/순간이동 궤적** | 무기 소켓 |  | |
| 7 | `P_Life_Resurrect` | **부활 폭발**(바람 러시+워프 구) — A2.4 | 얼굴 본 이미터 전부 삭제 |  | |
| 8 | `P_Life_Spawn` | **스폰/부활 지점 빛**(최경량) — `SC_ADD_OBJECT` | — |  | |

## 결정 기록

- (사용자 결정을 여기에 날짜와 함께 적고, FX_CATALOG.md 상태 열을 갱신한다)
