# IT2b 등급 외형 메시 제작 로그

흐름: `fb0fad37-d136-4ac6-b85c-006e9a98f533` (JH_MMO)
형식: 등급 · 부위 · 노드(컨셉 GI / 멀티뷰 EI / II = 채택 ImageInput / 3D) · 판정 · 재시도 · FBX 경로

## 기존 (00 양손검, 참고용 — 24개에 포함하지 않음)

- 00 · TwoHandSword · 컨셉 GI `b89361d3-e30e-4798-b3cd-a068fd494cde` / 채택 ImageInput `c4b9844f-77fa-4006-986c-b1bdfc61f137` / 멀티뷰 EI `9f716d1f-beba-46ee-b24b-86ae849d6db3` / 3D `08f69f00-cf72-40f9-a146-f715b7bc827d` · 판정: 멀티뷰 4면 일치, 강철 롱소드, 통과 · 3D 완료(30000 tri, 원본 정규화 bbox 세로 약 0.99 · 가로 0.165 · 두께 0.033 — UE에서 1.4m로 맞출 것) · FBX `UEClient/JM/SourceArt/IT2b/00_TwoHandSword.fbx` · 컨셉 `Concept/IT2b/00_TwoHandSword_concept.png`(기존 `_a`) · 멀티뷰 `Concept/IT2b/00_TwoHandSword_multiview.png` · 미채택 `_concept_b.png`. 확인 필요: 손잡이가 한 손 길이에 가까움(두 손 길이 요건과 비교 필요).

## 24개 부위 (완료 24 / 24 — FBX 전부 저장)

판정 기준: 멀티뷰 4면이 같은 물체인지 직접 확인, 등급 차이·양손검 비율 확인. 3D는 전부 30000 tri, tri, usePbrTexture 1, textureSize 2048, tPose 0.

### 01 (가죽)
- Helmet · GI d4933079 / EI cae6b3a2 / II f4bfdfd3 / 3D 2aebbfb5 · 통과 · `IT2b/01_Helmet.fbx`
- Top · GI 2d43a6b8 / EI e682ee6e / II c3cc0202 / 3D 20f5c584 · 통과 · `IT2b/01_Top.fbx`
- OneHandSword · GI 935aac5c / EI 60624f26 / II 7df76912 / 3D c1780ef4 · 통과 · `IT2b/01_OneHandSword.fbx`
- TwoHandSword · 1차 GI c233a35e 탈락(한손검과 비율 구분 안 됨, 손잡이 짧음) → 재시도 1회(같은 GI, 지시문: 날이 손잡이의 약 두 배, 긴 손잡이) 통과 · EI 91b5d66f / II 17dbbd66 / 3D bf618ae1 · `IT2b/01_TwoHandSword.fbx` · 재시도 컨셉 `Concept/IT2b/01_TwoHandSword_concept.png`
- Bow · GI 16efcda7 / EI a3bce8e0 / II a44bab08 / 3D 33cb4439 · 통과 · `IT2b/01_Bow.fbx`
- Shield · GI 52be1f07 / EI 5ed52c17 / II 6d926107 / 3D cc0e6625 · 통과 · `IT2b/01_Shield.fbx`
- Bottom · GI 71101814 / EI 43f2463d / II 77504855 / 3D 954a8fed · 통과 · `IT2b/01_Bottom.fbx` (02와 공유)
- Gloves · GI 3534945a / EI 0317c26c / II 3ebc1693 / 3D 301f9d7d · 통과 · `IT2b/01_Gloves.fbx` (02와 공유)
- Boots · GI a6b156f7 / EI efb956dc / II f8e858b4 / 3D 7200e19a · 통과 · `IT2b/01_Boots.fbx` (02와 공유)

### 02 (강철)
- Helmet · GI 7a0e602c / EI 3d12b7f4 / II 0de10dc7 / 3D 4a643fa0 · 통과 · `IT2b/02_Helmet.fbx`
- Top · GI fe4bcd97 / EI 751f2bd8 / II 2c3e5fe5 / 3D bacf6483 · 통과 · `IT2b/02_Top.fbx`
- OneHandSword · GI a520f2ef(첫 실행이 결과 없이 멈춰 재실행) / EI 8ffe286d / II 234e9a3e / 3D 04eaf1a3 · 통과 · `IT2b/02_OneHandSword.fbx`
- TwoHandSword · GI 86408a20 / EI f2148866 / II 7651504f / 3D 94bdfee0 · 통과(날이 살짝 휘어 보임, 한손검도 비슷한 실루엣 — 참고) · `IT2b/02_TwoHandSword.fbx`
- Bow · GI 9c63c7e7 / EI f6daa21d / II 06dd55d9 / 3D edcc67f5 · 통과 · `IT2b/02_Bow.fbx`
- Shield · GI e48b2180 / EI d1b94679 / II d77de7de / 3D 2473745e · 통과 · `IT2b/02_Shield.fbx`

### 03 (근위)
- Helmet · GI 21fd2428 / EI 3be885fd / II 0ec695ec / 3D 97f0e57b · 통과 · `IT2b/03_Helmet.fbx`
- Top · GI 3eee3db3 / EI f8dcff39 / II d8113d5f / 3D eb91fb82 · 통과 · `IT2b/03_Top.fbx`
- OneHandSword · GI 8208cc77 / EI b8eeb20f / II 3da2cd36 / 3D 215d5508 · 통과 · `IT2b/03_OneHandSword.fbx`
- TwoHandSword · 1차 GI 6f5c4aac 탈락(한손검과 비율 구분 안 됨) → 재시도 1회(같은 GI, 지시문 강화) 통과 · EI f0e07dfe / II 2f253514 / 3D 7ed9fb47 · `IT2b/03_TwoHandSword.fbx`
- Bow · GI a2ec1d56 / EI 070ac87d / II f4a9f67b / 3D 0f0f2e83 · 통과 · `IT2b/03_Bow.fbx`
- Shield · GI 1eb19da5 / EI 3eae4690 / II d31a3457 / 3D 16a74ba5 · 통과 · `IT2b/03_Shield.fbx`
- Bottom · GI 1e8847fb / EI d4830bef / II 08e93a12 / 3D 077a3e61 · 통과 · `IT2b/03_Bottom.fbx`
- Gloves · GI 80b1d7ef / EI 50dac303 / II b47f7176 / 3D 9516b84c · 통과 · `IT2b/03_Gloves.fbx`
- Boots · GI 8f0f0333 / EI 1b04ec37 / II dc8ec796 / 3D af60d5bb · 통과 · `IT2b/03_Boots.fbx`

컨셉·멀티뷰 이미지: `Concept/IT2b/<등급>_<부위>_concept.png`·`_multiview.png` (01 Bottom·Gloves·Boots, 02 공유분 제외 전부 저장).
시트: `Concept/IT2b/IT2b_lineup.png` (행 = 등급, 열 = 부위).

## 확인 메모

- 3D 완료 후 GLB 검사(메시 1개, 재질 1개)는 00 양손검과 01 투구에서 함. 나머지 23개는 FBX 헤더 확인과 멀티뷰 판정까지만 — 형상 최종 확인은 UE 반입 때.
- VARCO 3D 생성 결과의 원본 크기는 긴 변 약 1m 정규화. UE 반입 때 실제 크기 맞춤 필요(양손검은 약 1.4m).
- 02 양손검·한손검과 03 양손검은 부위별 판정 기준으로 비율 차이를 확인했으나, 완벽한 구분은 아님 — 사용자 육안 확인 권장.
- 멈춤 현상 세 번(02 한손검 GI, 01 양손검 EI, 03 활 EI): 결과 없이 비어 있다가 재실행하여 해결. 3D 생성은 40분 넘게 걸린 경우도 있었음.

## 누적 포인트 (run_flow 응답 합계)

- 시작 1,250 → 최종 **8,800** (상한 10,000 미만). 구성: GI·EI 이미지 약 50씩, 3D 200씩. 재실행·재시도 포함.
- 재시도: 01 양손검 1회, 03 양손검 1회. 멈춤 재실행: 02 한손검 GI 1회(50), 01 양손검 EI 1회(50), 03 활 EI 1회(50), 03 양손검 EI 1회(50).

## 양손검 다시 제작 (2026-11-22, D-256)

사용자: "양손검은 한손검보다 전체적으로 크고 날이 두껍게". 00~03 4개 모두. 이전 컨셉·멀티뷰는 `Concept/IT2b/<등급>_TwoHandSword_*_v1.png`로 보관.

방법: 지금 컨셉(320~512px JPEG로 업로드) → `EditImage` ×2장, 공용 지시문 TextInput `125660ec-db70-41c6-a5ef-b59d821de92d`(재질·장식 그대로, 날 폭 약 3배·두꺼운 단면·리카소·손잡이 약 1/4·넓고 두꺼운 가드·큰 폼멜, 30° 돌린 3/4 시점, 초록 배경). 한손검과 나란히 비교해 채택 → 멀티뷰(공용 TextInput `4d80fc35-a1ab-4a71-9c5f-3f3e263da35c`, 옆면은 두께가 보이게) → `Generate3D`(30000 tri, PBR, 2048).

| 등급 | 업로드 II | EditImage | 채택 | 채택 II | 멀티뷰 EI | 3D |
|---|---|---|---|---|---|---|
| 00 | 6d57f5e8-0f8d-4047-9729-78cb0949819b | 27355710-74a3-4fef-b570-15bf7cbe9f30 | 0번 | 679f9564-acbb-4043-8b0a-465c316bde82 | 25146c8c-7883-4248-abba-e99137e004b5 | ee4857c0-6c0e-4477-b78a-b8d3195bbdbb |
| 01 | 447372c8-54fb-4233-be50-bdfa9f90e0e7 | d6e6fa41-d9a0-4230-b732-07ceca3cb1e8 | 0번 | 7d547319-b35e-4fa9-a66d-660bd3f24407 | 5b1afefc-5286-4293-8051-1204bf7c7af1(첫 실행 멈춤 → 재실행) | 0acf681c-aaf5-4383-8155-93cbe255c302 |
| 02 | 9dd1e0ce-2da3-4fe0-8c2a-cece08f21650 | f26a79b8-14ac-4a97-9acb-f8008d607aca | 1번 | 5534c05e-aed5-4d54-8668-2f868679f364 | 04105f96-5165-42ef-a7a5-2470e7c6145b | ed9a696b-00ea-4f22-9098-20a9d6e2ae40 |
| 03 | 99aa3ef1-ebba-40ba-9768-2f6997d2cb36 | 662946e6-359d-4e6a-b17b-18ac7245dda6(2장 중 1장만 나옴) | 0번 | c909bd6e-7b68-4fb4-afb5-355c82dc2c44 | 2eb228f5-6cd4-49e8-95cc-d2e21d7ab6d3 | b5588eaf-d9ea-4819-984f-81e93cd90c62 |

판정: 컨셉은 같은 등급 한손검과 나란히 놓고 날 폭·두께·손잡이 길이 차이 확인(통과). 멀티뷰 00~03 모두 네 방향 같은 물체, 옆면에 두께 보임(통과).

포인트: 컨셉 400 + 멀티뷰 200 + 01 멀티뷰 재실행 50 + 3D(00·02·03) 600 = 1,250 + 01 3D 200 = 1,450 → 누적 약 10,250.

3D 결과(2026-11-22): 4개 모두 30000 tri·메시 1개. FBX를 `IT2b/<등급>_TwoHandSword.fbx`로 교체(이전 것은 `_v1.fbx`). 형상 확인은 `.claude/skills/varco-gear/scripts/glb_views.py`(GLB 세 방향 실루엣 + 비율, 외부 패키지 없음).

| 등급 | 3D taskId | 전체 bbox 폭·두께(긴 변 = 1) | 날 중간 폭·두께(길이 = 1) | 1.4m일 때 날 폭·두께 |
|---|---|---|---|---|
| 00 | 321f3aae-bf68-4fab-ade6-c04d1d6b08de | 0.305 · 0.070 | 0.100 · 0.032 | 14cm · 4.5cm |
| 01 | 0b7bd8ed-48c4-4ceb-8171-48f36e40ad5e | 0.289 · 0.080 | 0.130 · 0.051 | 18cm · 7cm |
| 02 | f3b7fee8-3502-4a41-9b7c-8ed31585833f | 0.302 · 0.071 | 0.093 · 0.025 | 13cm · 3.5cm |
| 03 | a4999bc3-2dd4-45f4-8b0e-803071c9ccb8 | 0.307 · 0.079 | 0.125 · 0.039 | 17.5cm · 5.5cm |

이전 00 양손검은 전체 bbox 폭 0.165·두께 0.033이었음 → 폭·두께 약 2배. 판정: 통과(사용자 육안 확인은 IT2c 진열·PIE에서).
