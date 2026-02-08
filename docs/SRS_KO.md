# 소프트웨어 요구사항 명세서 - PACS Bridge

> **버전:** 0.2.0.0
> **최종 수정일:** 2026-02-08
> **언어:** [English](SRS.md) | **한국어**
> **표준:** IEEE 830-1998 기반

---

## 문서 정보

| 항목 | 설명 |
|------|-------------|
| **문서 ID** | BRIDGE-SRS-001 |
| **프로젝트** | PACS Bridge |
| **작성자** | kcenon@naver.com |
| **상태** | 초안 |
| **관련 문서** | [PRD](PRD_KO.md), [참조 자료](reference_materials/README.md) |

---

## 목차

- [1. 서론](#1-서론)
- [2. 전체 설명](#2-전체-설명)
- [3. 세부 요구사항](#3-세부-요구사항)
- [4. 외부 인터페이스 요구사항](#4-외부-인터페이스-요구사항)
- [5. 시스템 기능](#5-시스템-기능)
- [6. 비기능 요구사항](#6-비기능-요구사항)
- [7. 요구사항 추적성 매트릭스](#7-요구사항-추적성-매트릭스)
- [8. 부록](#8-부록)

---

## 1. 서론

### 1.1 목적

이 소프트웨어 요구사항 명세서(SRS)는 PACS Bridge 시스템의 모든 소프트웨어 요구사항에 대한 완전한 설명을 제공합니다. 이 문서는 이해관계자와 개발팀 간의 소프트웨어 제품 기능에 대한 합의의 기반을 제공합니다.

### 1.2 범위

PACS Bridge는 다음 기능을 제공하는 의료 통합 게이트웨이입니다:
- HL7 v2.x 및 FHIR R4 메시징과 DICOM 서비스 간 브릿지
- HIS/RIS의 오더를 DICOM Modality Worklist 항목으로 변환
- MPPS 알림을 HL7 상태 업데이트로 변환
- IHE Scheduled Workflow (SWF) 통합 프로파일 구현
- pacs_system MWL/MPPS 서비스와 네이티브 통합

### 1.3 정의, 약어 및 약자

| 용어 | 정의 |
|------|------------|
| **HIS** | 병원 정보 시스템 (Hospital Information System) |
| **RIS** | 영상의학 정보 시스템 (Radiology Information System) |
| **EMR/EHR** | 전자 의무 기록 (Electronic Medical/Health Records) |
| **HL7** | Health Level Seven International 메시징 표준 |
| **FHIR** | Fast Healthcare Interoperability Resources |
| **MLLP** | 최소 하위 계층 프로토콜 (HL7 전송) |
| **ADT** | 입원, 퇴원, 전원 (HL7 메시지 유형) |
| **ORM** | 오더 메시지 (HL7 메시지 유형) |
| **ORU** | 관찰 결과 (HL7 메시지 유형) |
| **SIU** | 스케줄링 정보 (HL7 메시지 유형) |
| **ACK** | 확인 응답 (HL7 메시지 유형) |
| **MWL** | Modality Worklist |
| **MPPS** | Modality Performed Procedure Step |
| **IHE** | Integrating the Healthcare Enterprise |
| **SWF** | Scheduled Workflow (IHE 프로파일) |
| **SCP** | Service Class Provider |
| **SCU** | Service Class User |

### 1.4 참조

| 참조 | 설명 |
|-----------|-------------|
| HL7 v2.5.1 | HL7 버전 2.5.1 명세 |
| FHIR R4 | HL7 FHIR Release 4 명세 |
| DICOM PS3.4 | DICOM 서비스 클래스 명세 |
| IHE RAD TF-1/2 | IHE 영상의학 기술 프레임워크 |
| PRD-BRIDGE-001 | 제품 요구사항 문서 |
| pacs_system SRS | pacs_system 소프트웨어 요구사항 명세서 |

### 1.5 개요

이 문서의 구성:
- **섹션 2**: 전체 시스템 설명 및 제약사항
- **섹션 3**: 추적성을 갖춘 상세 기능 요구사항
- **섹션 4**: 외부 인터페이스 요구사항
- **섹션 5**: 시스템 기능 명세
- **섹션 6**: 비기능 요구사항
- **섹션 7**: 요구사항 추적성 매트릭스
- **섹션 8**: 부록

---

## 2. 전체 설명

### 2.1 제품 관점

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        의료 통합 생태계                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐             │
│    │     HIS      │    │     RIS      │    │   EMR/EHR    │             │
│    │   (HL7 v2.x) │    │  (HL7 v2.x)  │    │  (FHIR R4)   │             │
│    └───────┬──────┘    └───────┬──────┘    └───────┬──────┘             │
│            │                   │                   │                     │
│            └───────────────────┼───────────────────┘                     │
│                                │ ADT/ORM/ORU/SIU                         │
│                                ▼                                         │
│    ┌─────────────────────────────────────────────────────────────────┐  │
│    │                        PACS Bridge                               │  │
│    │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌───────────┐  │  │
│    │  │ HL7 Gateway │ │FHIR Gateway │ │메시지 라우터 │ │변환 계층   │  │  │
│    │  │ (MLLP)      │ │ (REST)      │ │& 큐         │ │           │  │  │
│    │  └──────┬──────┘ └──────┬──────┘ └──────┬──────┘ └─────┬─────┘  │  │
│    │         └───────────────┴───────────────┴───────────────┘        │  │
│    │                              │                                   │  │
│    │  ┌───────────────────────────▼──────────────────────────────┐   │  │
│    │  │                  pacs_system 어댑터                        │   │  │
│    │  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────┐  │   │  │
│    │  │  │ MWL 클라이언트│ │MPPS 핸들러  │ │ 환자 캐시            │  │   │  │
│    │  │  └─────────────┘ └─────────────┘ └─────────────────────┘  │   │  │
│    │  └──────────────────────────────────────────────────────────┘   │  │
│    └─────────────────────────────────────────────────────────────────┘  │
│                                │                                         │
│                                │ DICOM (MWL/MPPS)                        │
│                                ▼                                         │
│    ┌─────────────────────────────────────────────────────────────────┐  │
│    │                        pacs_system                               │  │
│    │  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────────────┐  │  │
│    │  │ Worklist SCP│ │  MPPS SCP   │ │    인덱스 데이터베이스        │  │  │
│    │  └─────────────┘ └─────────────┘ └─────────────────────────────┘  │  │
│    └─────────────────────────────────────────────────────────────────┘  │
│                                │                                         │
│            ┌───────────────────┼───────────────────┐                     │
│            ▼                   ▼                   ▼                     │
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐             │
│    │   모달리티    │    │   모달리티    │    │   모달리티    │             │
│    │  (CT/MR/US)  │    │  (CT/MR/US)  │    │  (CT/MR/US)  │             │
│    └──────────────┘    └──────────────┘    └──────────────┘             │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 제품 기능

| 기능 | 설명 |
|----------|-------------|
| **HL7 메시지 처리** | ADT, ORM, ORU, SIU 메시지 파싱 및 처리 |
| **FHIR 리소스 처리** | Patient, ServiceRequest, ImagingStudy 리소스 처리 |
| **프로토콜 변환** | HL7, FHIR, DICOM 형식 간 변환 |
| **MWL 관리** | Modality Worklist 항목 생성 및 업데이트 |
| **MPPS 알림** | MPPS 이벤트 수신 및 HIS/RIS 알림 |
| **메시지 라우팅** | 장애 조치를 포함한 시스템 간 메시지 라우팅 |
| **큐 관리** | 보장된 전달을 위한 발신 메시지 큐잉 |

### 2.3 사용자 클래스 및 특성

| 사용자 클래스 | 특성 | 기술 수준 |
|------------|-----------------|-----------------|
| 통합 엔지니어 | HL7/DICOM 매핑 구성 | 전문가 |
| 영상의학과 IT 관리자 | 워크리스트 및 라우팅 관리 | 고급 |
| 의료장비 업체 | 모달리티 소프트웨어 통합 | 고급 |
| 시스템 관리자 | 시스템 상태 모니터링 | 중급 |

### 2.4 운영 환경

| 구성요소 | 요구사항 |
|-----------|-------------|
| **운영체제** | Linux (Ubuntu 22.04+), macOS 14+, Windows 10/11 |
| **컴파일러** | C++20 (GCC 11+, Clang 14+, MSVC 2022+) |
| **메모리** | 최소 2 GB, 권장 8 GB |
| **네트워크** | 최소 100 Mbps 이더넷 |
| **의존성** | pacs_system v0.2.0+, kcenon 생태계 |

### 2.5 설계 및 구현 제약사항

| 제약사항 | 설명 |
|------------|-------------|
| **C1** | DICOM 서비스에 pacs_system 사용 필수 |
| **C2** | kcenon 생태계 컴포넌트 사용 필수 |
| **C3** | 크로스 플랫폼 호환성 필수 |
| **C4** | HL7 v2.3 ~ v2.5.1 호환성 필수 |
| **C5** | IHE SWF 프로파일 적합성 필수 |
| **C6** | BSD 3-Clause 라이선스 호환성 |

### 2.6 가정 및 의존성

| ID | 가정/의존성 |
|----|----------------------|
| **A1** | pacs_system MWL/MPPS 서비스 운영 중 |
| **A2** | network_system이 안정적인 MLLP 전송 제공 |
| **A3** | HIS/RIS 시스템이 표준 HL7 v2.x 구현 |
| **A4** | FHIR 엔드포인트가 R4 명세 준수 |
| **D1** | pacs_system v0.2.0+ 가용 |
| **D2** | common_system v1.0+ 가용 |
| **D3** | container_system v1.0+ 가용 |
| **D4** | network_system v1.0+ 가용 |
| **D5** | thread_system v1.0+ 가용 |
| **D6** | logger_system v1.0+ 가용 |

---

## 3. 세부 요구사항

### 3.1 HL7 게이트웨이 모듈 요구사항

#### SRS-HL7-001: HL7 v2.x 메시지 파싱
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-HL7-001 |
| **제목** | HL7 v2.x 메시지 파서 |
| **설명** | 시스템은 표준 구분자, 이스케이프 시퀀스, 반복 필드, 커스텀 Z-세그먼트를 지원하여 HL7 v2.x 메시지(v2.3 ~ v2.5.1)를 파싱해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-1.1.1 - FR-1.1.5 |

**인수 기준:**
1. 표준 세그먼트 구분자 파싱 (|^~\&)
2. 이스케이프 시퀀스 처리 (\F\, \S\, \R\, \E\, \T\)
3. ~ 구분자를 사용한 반복 필드 지원
4. 컴포넌트/서브컴포넌트 분리 지원
5. 모든 표준 세그먼트 데이터 추출

**메시지 구조:**
```
MSH|^~\&|SendingApp|SendingFac|ReceivingApp|ReceivingFac|DateTime||MessageType|ControlID|P|Version
PID|Set ID|External ID|Patient ID|Alt Patient ID|Patient Name|...
ORC|Order Control|Placer Order|Filler Order|...
OBR|Set ID|Placer Order|Filler Order|Universal Service ID|...
```

---

#### SRS-HL7-002: ADT 메시지 처리
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-HL7-002 |
| **제목** | ADT 메시지 핸들러 |
| **설명** | 시스템은 환자 인구통계 캐시를 유지하고 MWL 항목을 업데이트하기 위해 ADT 메시지(A01, A04, A08, A40)를 처리해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-1.2.1 |

**인수 기준:**
1. ADT^A01 (입원) 처리 - 환자 레코드 생성
2. ADT^A04 (등록) 처리 - 외래 환자 레코드 생성
3. ADT^A08 (업데이트) 처리 - 환자 인구통계 업데이트
4. ADT^A40 (병합) 처리 - 환자 레코드 병합
5. 환자 업데이트 시 모든 관련 MWL 항목 업데이트

**ADT-환자 캐시 매핑:**

| HL7 필드 | 캐시 필드 | 설명 |
|-----------|-------------|-------------|
| PID-3 | patient_id | 환자 식별자 |
| PID-3.4 | issuer | 발급 기관 |
| PID-5 | patient_name | 환자 이름 (XPN) |
| PID-7 | birth_date | 생년월일 |
| PID-8 | sex | 성별 |
| PID-11 | address | 환자 주소 |
| PID-13 | phone | 연락처 |

---

#### SRS-HL7-003: ORM 메시지 처리
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-HL7-003 |
| **제목** | ORM 오더 메시지 핸들러 |
| **설명** | 시스템은 오더 제어 코드를 기반으로 MWL 항목을 생성, 업데이트, 취소 및 관리하기 위해 ORM^O01 메시지를 처리해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-1.2.2, FR-3.1.1 - FR-3.1.6 |

**인수 기준:**
1. ORC-1 = NW (신규 오더) 시 MWL 항목 생성
2. ORC-1 = XO (오더 변경) 시 MWL 항목 업데이트
3. ORC-1 = CA (취소 요청) 시 MWL 항목 취소
4. ORC-1 = SC (상태 변경) 시 상태 변경 처리
5. ORC-1 = DC (중단) 시 중단 지원

**오더 제어 처리:**

| ORC-1 | ORC-5 | 동작 | MWL 상태 |
|-------|-------|--------|------------|
| NW | SC | 신규 항목 생성 | SCHEDULED |
| NW | IP | 생성 후 시작 표시 | STARTED |
| XO | SC | 기존 항목 업데이트 | SCHEDULED |
| XO | IP | 업데이트 후 시작 표시 | STARTED |
| CA | CA | 항목 삭제 | (삭제됨) |
| DC | CA | 항목 중단 | DISCONTINUED |
| SC | IP | 상태 → 진행 중 | STARTED |
| SC | CM | 상태 → 완료 | COMPLETED |

---

#### SRS-HL7-004: ORU 메시지 생성
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-HL7-004 |
| **제목** | ORU 결과 메시지 생성기 |
| **설명** | 시스템은 예비 및 최종 리포트 상태를 포함한 리포트 상태 알림을 위해 ORU^R01 메시지를 생성해야 한다. |
| **우선순위** | 권장 |
| **단계** | 2 |
| **추적** | FR-1.2.3, FR-3.3.1 - FR-3.3.4 |

**인수 기준:**
1. 예비 리포트용 ORU^R01 생성 (OBR-25 = P)
2. 최종 리포트용 ORU^R01 생성 (OBR-25 = F)
3. LOINC 코드가 포함된 적절한 OBX 세그먼트 포함
4. 리포트 수정 지원 (OBR-25 = C)
5. 의뢰 의사 정보 포함

---

#### SRS-HL7-005: SIU 메시지 처리
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-HL7-005 |
| **제목** | SIU 스케줄링 메시지 핸들러 |
| **설명** | 시스템은 예약 생성, 수정 및 취소를 지원하여 스케줄링 업데이트를 위해 SIU^S12-S15 메시지를 처리해야 한다. |
| **우선순위** | 권장 |
| **단계** | 2 |
| **추적** | FR-1.2.4 |

**인수 기준:**
1. SIU^S12 (신규 예약) 처리
2. SIU^S13 (재예약) 처리
3. SIU^S14 (수정) 처리
4. SIU^S15 (취소) 처리
5. MWL 스케줄링 정보 업데이트

---

#### SRS-HL7-006: ACK 응답 생성
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-HL7-006 |
| **제목** | 확인 응답 생성기 |
| **설명** | 시스템은 적절한 오류 코드와 설명을 포함한 올바른 HL7 ACK 응답(AA, AE, AR)을 생성해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-1.2.5 |

**인수 기준:**
1. 성공 시 AA (Application Accept) 생성
2. 비즈니스 로직 오류 시 AE (Application Error) 생성
3. 시스템 오류 시 AR (Application Reject) 생성
4. 오류 세부 정보가 포함된 ERR 세그먼트 포함
5. MSA-2에 원본 MSH-10 에코

**ACK 응답 구조:**
```
MSH|^~\&|PACS|RADIOLOGY|HIS|HOSPITAL|DateTime||ACK^O01|ControlID|P|2.5.1
MSA|AA|OriginalMsgID|Message accepted
```

```
MSH|^~\&|PACS|RADIOLOGY|HIS|HOSPITAL|DateTime||ACK^O01|ControlID|P|2.5.1
MSA|AE|OriginalMsgID|Missing required field
ERR|||207^Application internal error^HL70357|E|||OBR-4 required
```

---

### 3.2 MLLP 전송 요구사항

#### SRS-MLLP-001: MLLP 서버 구현
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-MLLP-001 |
| **제목** | MLLP 서버 (리스너) |
| **설명** | 시스템은 수신 HL7 연결을 대기하고, 메시지 프레이밍을 처리하며, 동시 연결을 지원하는 MLLP 서버를 구현해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-1.3.1, FR-1.3.3, FR-1.3.4 |

**인수 기준:**
1. 구성 가능한 TCP 포트에서 대기 (기본 2575)
2. MLLP 프레이밍 처리 (VT=0x0B, FS=0x1C, CR=0x0D)
3. 동시 연결 지원 (≥50)
4. 구성 가능한 연결 타임아웃
5. 연결 유지(Keep-alive) 지원

**MLLP 프레임 구조:**
```
<VT>HL7 Message Content<FS><CR>

VT = 0x0B (Vertical Tab)
FS = 0x1C (File Separator)
CR = 0x0D (Carriage Return)
```

---

#### SRS-MLLP-002: MLLP 클라이언트 구현
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-MLLP-002 |
| **제목** | MLLP 클라이언트 (발신자) |
| **설명** | 시스템은 연결 풀링과 재시도 지원을 통해 원격 시스템에 HL7 메시지를 전송하기 위한 MLLP 클라이언트를 구현해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-1.3.2, FR-1.3.3 |

**인수 기준:**
1. 구성 가능한 호스트 및 포트에 연결
2. 발신 메시지에 MLLP 프레이밍 적용
3. ACK 응답 대기 및 검증
4. 지속 연결을 위한 연결 풀링
5. 지수 백오프를 통한 구성 가능한 재시도

---

#### SRS-MLLP-003: TLS를 통한 MLLP
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-MLLP-003 |
| **제목** | 보안 MLLP 전송 |
| **설명** | 시스템은 인증서 검증을 통해 안전한 HL7 메시지 전송을 위해 TLS 1.2/1.3을 통한 MLLP를 지원해야 한다. |
| **우선순위** | 권장 |
| **단계** | 2 |
| **추적** | FR-1.3.5, NFR-4.1 |

**인수 기준:**
1. TLS 1.2 및 TLS 1.3 지원
2. 서버 인증서 검증
3. 선택적 클라이언트 인증서 (상호 TLS)
4. 구성 가능한 암호화 스위트
5. 인증서 체인 검증

---

### 3.3 FHIR 게이트웨이 요구사항

#### SRS-FHIR-001: FHIR REST 서버
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-FHIR-001 |
| **제목** | FHIR R4 REST API 서버 |
| **설명** | 시스템은 표준 CRUD 작업과 함께 JSON 및 XML 형식을 지원하는 FHIR R4 호환 REST 서버를 구현해야 한다. |
| **우선순위** | 권장 |
| **단계** | 3 |
| **추적** | FR-2.1.1 - FR-2.1.5 |

**인수 기준:**
1. 리소스용 RESTful API 엔드포인트
2. JSON 및 XML 콘텐츠 협상
3. 표준 HTTP 메서드 (GET, POST, PUT, DELETE)
4. 검색 파라미터 지원
5. 대용량 결과 집합에 대한 페이지네이션

**FHIR 기본 URL 구조:**
```
https://server/fhir/r4/{ResourceType}/{id}
```

---

#### SRS-FHIR-002: Patient 리소스
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-FHIR-002 |
| **제목** | FHIR Patient 리소스 지원 |
| **설명** | 시스템은 내부 환자 캐시에 매핑하여 읽기 및 검색 작업이 가능한 FHIR Patient 리소스를 지원해야 한다. |
| **우선순위** | 권장 |
| **단계** | 3 |
| **추적** | FR-2.2.1 |

**인수 기준:**
1. GET /Patient/{id} - 환자 조회
2. GET /Patient?identifier=xxx - ID로 검색
3. GET /Patient?name=xxx - 이름으로 검색
4. 내부 환자 인구통계에 매핑
5. 환자 식별자 시스템 지원

---

#### SRS-FHIR-003: ServiceRequest 리소스
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-FHIR-003 |
| **제목** | FHIR ServiceRequest (영상 오더) |
| **설명** | 시스템은 수신 요청에서 MWL 항목을 생성하여 영상 오더를 위한 FHIR ServiceRequest를 지원해야 한다. |
| **우선순위** | 권장 |
| **단계** | 3 |
| **추적** | FR-2.2.2 |

**인수 기준:**
1. POST /ServiceRequest - 오더 생성
2. GET /ServiceRequest/{id} - 오더 조회
3. GET /ServiceRequest?patient=xxx - 환자로 검색
4. DICOM MWL 항목에 매핑
5. 오더 상태 업데이트 지원

---

#### SRS-FHIR-004: ImagingStudy 리소스
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-FHIR-004 |
| **제목** | FHIR ImagingStudy 리소스 |
| **설명** | 시스템은 스터디 가용성 알림 및 쿼리를 위해 FHIR ImagingStudy 리소스를 지원해야 한다. |
| **우선순위** | 권장 |
| **단계** | 3 |
| **추적** | FR-2.2.3, FR-2.3.2 |

**인수 기준:**
1. GET /ImagingStudy/{id} - 스터디 조회
2. GET /ImagingStudy?patient=xxx - 환자로 검색
3. GET /ImagingStudy?status=xxx - 상태로 검색
4. 시리즈 및 인스턴스 개수 포함
5. ServiceRequest 참조

---

### 3.4 프로토콜 변환 요구사항

#### SRS-TRANS-001: HL7-DICOM MWL 매퍼
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-TRANS-001 |
| **제목** | HL7 ORM에서 DICOM MWL로 변환 |
| **설명** | 시스템은 IHE SWF 프로파일 매핑에 따라 HL7 ORM 메시지 필드를 DICOM Modality Worklist 속성으로 변환해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-3.1.1 - FR-3.1.4 |

**인수 기준:**
1. PID 세그먼트를 Patient Module에 매핑
2. ORC/OBR을 Imaging Service Request에 매핑
3. OBR을 Scheduled Procedure Step Sequence에 매핑
4. ZDS-1을 Study Instance UID에 매핑
5. 프로시저 코드 매핑 처리

**핵심 매핑 테이블:**

| HL7 필드 | DICOM 태그 | DICOM 키워드 |
|-----------|-----------|---------------|
| PID-3 | (0010,0020) | PatientID |
| PID-3.4 | (0010,0021) | IssuerOfPatientID |
| PID-5 | (0010,0010) | PatientName |
| PID-7 | (0010,0030) | PatientBirthDate |
| PID-8 | (0010,0040) | PatientSex |
| ORC-2 | (0040,2016) | PlacerOrderNumberImagingServiceRequest |
| ORC-3 | (0008,0050) | AccessionNumber |
| ORC-12 | (0008,0090) | ReferringPhysicianName |
| OBR-4.1 | (0008,0100) | CodeValue (RequestedProcedureCodeSequence 내) |
| OBR-4.2 | (0008,0104) | CodeMeaning |
| OBR-4.3 | (0008,0102) | CodingSchemeDesignator |
| OBR-7 | (0040,0002) | ScheduledProcedureStepStartDate |
| OBR-7 | (0040,0003) | ScheduledProcedureStepStartTime |
| OBR-24 | (0008,0060) | Modality |
| ZDS-1 | (0020,000D) | StudyInstanceUID |

---

#### SRS-TRANS-002: DICOM MPPS-HL7 ORM 매퍼
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-TRANS-002 |
| **제목** | DICOM MPPS에서 HL7 ORM으로 변환 |
| **설명** | 시스템은 DICOM MPPS 알림을 HL7 ORM 상태 업데이트 메시지로 변환해야 한다. |
| **우선순위** | 필수 |
| **단계** | 2 |
| **추적** | FR-3.2.1 - FR-3.2.5 |

**인수 기준:**
1. MPPS IN PROGRESS를 ORC-5 = IP에 매핑
2. MPPS COMPLETED를 ORC-5 = CM에 매핑
3. MPPS DISCONTINUED를 ORC-5 = CA에 매핑
4. 프로시저 타이밍 정보 포함
5. 원본 오더 참조

**MPPS 상태 매핑:**

| MPPS 상태 | ORC-1 | ORC-5 | 설명 |
|-------------|-------|-------|-------------|
| IN PROGRESS | SC | IP | 검사 시작됨 |
| COMPLETED | SC | CM | 검사 완료됨 |
| DISCONTINUED | DC | CA | 검사 중단됨 |

---

#### SRS-TRANS-003: 환자 이름 형식 변환
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-TRANS-003 |
| **제목** | 환자 이름 형식 변환 |
| **설명** | 시스템은 구성요소 순서 차이를 처리하여 HL7 XPN과 DICOM PN 이름 형식 간 변환을 수행해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-3.1.2 |

**인수 기준:**
1. HL7 XPN 형식 파싱 (Family^Given^Middle^Suffix^Prefix)
2. DICOM PN 형식으로 변환 (Family^Given^Middle^Prefix^Suffix)
3. 누락된 구성요소 처리
4. 다중 값 이름 지원
5. 특수 문자 보존

**형식 변환:**
```
HL7 XPN: DOE^JOHN^ANDREW^JR^MR^MD
         Family^Given^Middle^Suffix^Prefix^Degree

DICOM PN: DOE^JOHN^ANDREW^MR^JR
          Family^Given^Middle^Prefix^Suffix
```

---

#### SRS-TRANS-004: 날짜/시간 형식 변환
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-TRANS-004 |
| **제목** | 날짜/시간 형식 변환 |
| **설명** | 시스템은 HL7 DTM과 DICOM DA/TM 날짜/시간 형식 간 변환을 수행해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-3.1.3 |

**인수 기준:**
1. HL7 DTM 파싱 (YYYYMMDDHHMMSS.SSSS±ZZZZ)
2. DICOM DA 추출 (YYYYMMDD)
3. DICOM TM 추출 (HHMMSS.FFFFFF)
4. 시간대 변환 처리
5. 부분 정밀도 처리

---

#### SRS-TRANS-005: FHIR-DICOM 매퍼
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-TRANS-005 |
| **제목** | FHIR ServiceRequest에서 DICOM MWL로 변환 |
| **설명** | 시스템은 FHIR ServiceRequest 리소스를 DICOM MWL 항목으로 변환해야 한다. |
| **우선순위** | 권장 |
| **단계** | 3 |
| **추적** | FR-2.2.2 |

**인수 기준:**
1. Patient 참조를 Patient Module에 매핑
2. ServiceRequest.code를 Requested Procedure에 매핑
3. ServiceRequest.occurrence를 SPS Start Date/Time에 매핑
4. ServiceRequest.performer를 Scheduled AE Title에 매핑
5. 제공되지 않은 경우 Study Instance UID 생성

---

### 3.5 메시지 라우팅 요구사항

#### SRS-ROUTE-001: 수신 메시지 라우터
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-ROUTE-001 |
| **제목** | 수신 메시지 라우팅 |
| **설명** | 시스템은 메시지 유형 및 트리거 이벤트를 기반으로 수신 메시지를 적절한 핸들러로 라우팅해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-4.1.1 - FR-4.1.4 |

**인수 기준:**
1. ADT 메시지를 환자 캐시 핸들러로 라우팅
2. ORM 메시지를 MWL 관리자로 라우팅
3. MSH-9 (메시지 유형)을 기반으로 라우팅
4. 조건부 라우팅 규칙 지원
5. 라우팅 결정 로깅

**라우팅 테이블:**

| 메시지 유형 | 트리거 | 핸들러 |
|--------------|---------|---------|
| ADT | A01, A04, A08, A40 | PatientCacheHandler |
| ORM | O01 | MWLManager |
| SIU | S12-S15 | SchedulingHandler |

---

#### SRS-ROUTE-002: 발신 메시지 라우터
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-ROUTE-002 |
| **제목** | 발신 메시지 라우팅 |
| **설명** | 시스템은 장애 조치 지원과 함께 구성된 대상으로 발신 메시지를 라우팅해야 한다. |
| **우선순위** | 필수 |
| **단계** | 2 |
| **추적** | FR-4.2.1 - FR-4.2.4 |

**인수 기준:**
1. MPPS 알림을 RIS로 라우팅
2. ORU 리포트를 구성된 엔드포인트로 라우팅
3. 다중 대상 지원
4. 장애 조치 라우팅 구현
5. 전달 상태 추적

---

#### SRS-ROUTE-003: 메시지 큐 관리자
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-ROUTE-003 |
| **제목** | 발신 메시지 큐 |
| **설명** | 시스템은 재시도 로직과 지속성을 통해 신뢰성 있는 전달을 위해 발신 메시지를 큐잉해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-4.3.1 - FR-4.3.4 |

**인수 기준:**
1. 비동기 전달을 위한 메시지 큐잉
2. 지수 백오프를 통한 재시도
3. 메시지 우선순위 지원
4. 충돌 복구를 위한 큐 지속성
5. 실패한 메시지를 위한 데드 레터 큐

**재시도 전략:**
```
시도 1: 즉시
시도 2: 5초
시도 3: 30초
시도 4: 2분
시도 5: 10분
최대 시도: 5 (구성 가능)
```

---

### 3.6 pacs_system 통합 요구사항

#### SRS-PACS-001: MWL 항목 관리
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-PACS-001 |
| **제목** | Modality Worklist 통합 |
| **설명** | 시스템은 DICOM 또는 직접 데이터베이스 접근을 통해 pacs_system에서 MWL 항목을 생성, 업데이트 및 삭제해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | IR-1, FR-3.1.1 |

**인수 기준:**
1. ORM^O01에서 MWL 항목 생성
2. 오더 변경 시 MWL 항목 업데이트
3. 취소 시 MWL 항목 삭제
4. 중복 감지 처리
5. 배치 작업 지원

---

#### SRS-PACS-002: MPPS 이벤트 처리
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-PACS-002 |
| **제목** | MPPS 알림 수신기 |
| **설명** | 시스템은 pacs_system에서 MPPS N-CREATE 및 N-SET 알림을 수신하고 HL7 메시지로 변환해야 한다. |
| **우선순위** | 필수 |
| **단계** | 2 |
| **추적** | IR-1, FR-3.2.1 - FR-3.2.5 |

**인수 기준:**
1. MPPS 이벤트 리스너로 등록
2. N-CREATE (IN PROGRESS) 수신
3. N-SET (COMPLETED/DISCONTINUED) 수신
4. HL7 ORM 상태 메시지 생성
5. 구성된 RIS 엔드포인트로 전송

---

#### SRS-PACS-003: 환자 캐시 동기화
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-PACS-003 |
| **제목** | 환자 인구통계 캐시 |
| **설명** | 시스템은 MWL 쿼리를 위해 HIS ADT 이벤트와 동기화된 환자 인구통계 캐시를 유지해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-4.1.1 |

**인수 기준:**
1. ADT에서 환자 인구통계 캐싱
2. ADT^A08 (업데이트) 시 업데이트
3. ADT^A40 (병합) 처리
4. Patient ID로 조회 제공
5. 시간 기반 캐시 만료

---

### 3.7 구성 요구사항

#### SRS-CFG-001: 엔드포인트 구성
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-CFG-001 |
| **제목** | 시스템 엔드포인트 구성 |
| **설명** | 시스템은 HL7 리스너 포트, 발신 대상 및 pacs_system 연결 구성을 지원해야 한다. |
| **우선순위** | 필수 |
| **단계** | 1 |
| **추적** | FR-5.1.1 - FR-5.1.4 |

**인수 기준:**
1. HL7 리스너 포트 구성
2. 발신 HL7 대상 구성
3. pacs_system 연결 구성
4. 구성 파일 지원 (YAML)
5. 핫 리로드 지원 (3단계)

**구성 예시:**
```yaml
server:
  name: "PACS_BRIDGE"

hl7:
  listener:
    port: 2575
    tls: false
    max_connections: 50

  outbound:
    - name: "RIS"
      host: "ris.hospital.local"
      port: 2576
      retry_count: 3

pacs_system:
  host: "localhost"
  port: 11112
  ae_title: "PACS_BRIDGE"
```

---

#### SRS-CFG-002: 매핑 구성
| 속성 | 값 |
|-----------|-------|
| **ID** | SRS-CFG-002 |
| **제목** | 프로토콜 매핑 구성 |
| **설명** | 시스템은 모달리티-AE-타이틀, 프로시저 코드 및 환자 ID 도메인에 대한 구성 가능한 매핑을 지원해야 한다. |
| **우선순위** | 권장 |
| **단계** | 2 |
| **추적** | FR-5.2.1 - FR-5.2.4 |

**인수 기준:**
1. 모달리티-AE 타이틀 매핑 구성
2. 프로시저 코드 매핑 구성
3. 환자 ID 도메인 매핑 구성
4. 커스텀 필드 매핑 지원
5. 로드 시 구성 검증

---

## 4. 외부 인터페이스 요구사항

### 4.1 사용자 인터페이스

PACS Bridge는 직접적인 사용자 인터페이스를 제공하지 않습니다. 모든 상호작용은 다음을 통해 이루어집니다:
- HL7 v2.x 메시징 (MLLP)
- FHIR R4 REST API (HTTPS)
- 구성 파일 (YAML)
- 관리 CLI (향후)

### 4.2 하드웨어 인터페이스

| 인터페이스 | 설명 |
|-----------|-------------|
| 네트워크 | 최소 100 Mbps 이더넷 |
| 메모리 | 최소 2 GB, 권장 8 GB |
| 스토리지 | 큐 지속성을 위해 10 GB |

### 4.3 소프트웨어 인터페이스

| 인터페이스 | 프로토콜 | 설명 |
|-----------|----------|-------------|
| HIS | HL7 v2.x / MLLP | 병원 정보 시스템 |
| RIS | HL7 v2.x / MLLP | 영상의학 정보 시스템 |
| EMR | FHIR R4 / HTTPS | 전자 의무 기록 |
| pacs_system | DICOM | PACS 서비스 |

### 4.4 통신 인터페이스

| 프로토콜 | 포트 | 설명 |
|----------|------|-------------|
| HL7/MLLP | 2575 | 수신 HL7 메시지 |
| HL7/MLLP | 가변 | RIS로 발신 |
| HTTPS | 8080 | FHIR REST API |
| DICOM | 11112 | pacs_system 연결 |

---

## 5. 시스템 기능

### 5.1 기능: HL7 오더 처리

**설명:** HIS/RIS에서 영상 오더를 수신하고 처리합니다.

| 속성 | 값 |
|-----------|-------|
| **우선순위** | 필수 |
| **단계** | 1 |
| **SRS 요구사항** | SRS-HL7-001, SRS-HL7-003, SRS-MLLP-001, SRS-TRANS-001 |
| **PRD 요구사항** | FR-1.1.x, FR-1.2.2, FR-3.1.x |

**사용 사례:**
1. RIS가 MLLP를 통해 ORM^O01 (신규 오더) 전송
2. PACS Bridge가 메시지 파싱 및 검증
3. HL7 필드를 DICOM 속성으로 변환
4. pacs_system에 MWL 항목 생성
5. RIS에 ACK 응답 전송

---

### 5.2 기능: Modality Worklist 브릿지

**설명:** DICOM C-FIND를 통해 모달리티에 워크리스트 데이터를 제공합니다.

| 속성 | 값 |
|-----------|-------|
| **우선순위** | 필수 |
| **단계** | 1 |
| **SRS 요구사항** | SRS-PACS-001, SRS-TRANS-001 |
| **PRD 요구사항** | FR-3.1.1 - FR-3.1.6 |

**사용 사례:**
1. 모달리티가 pacs_system MWL SCP를 쿼리
2. pacs_system이 내부 MWL 데이터베이스 쿼리
3. PACS Bridge가 생성한 MWL 항목 반환
4. 모달리티가 환자/프로시저 정보 수신

---

### 5.3 기능: MPPS 알림

**설명:** 프로시저 상태 변경을 RIS에 알립니다.

| 속성 | 값 |
|-----------|-------|
| **우선순위** | 필수 |
| **단계** | 2 |
| **SRS 요구사항** | SRS-PACS-002, SRS-TRANS-002, SRS-MLLP-002 |
| **PRD 요구사항** | FR-3.2.1 - FR-3.2.5 |

**사용 사례:**
1. 모달리티가 pacs_system에 MPPS N-CREATE (IN PROGRESS) 전송
2. pacs_system이 PACS Bridge에 알림
3. PACS Bridge가 ORM^O01 (ORC-5=IP)로 변환
4. MLLP를 통해 RIS에 상태 업데이트 전송
5. RIS가 ACK로 확인

---

### 5.4 기능: FHIR 통합

**설명:** 현대 EMR 통합을 위한 REST API를 제공합니다.

| 속성 | 값 |
|-----------|-------|
| **우선순위** | 권장 |
| **단계** | 3 |
| **SRS 요구사항** | SRS-FHIR-001 - SRS-FHIR-004, SRS-TRANS-005 |
| **PRD 요구사항** | FR-2.1.x, FR-2.2.x |

**사용 사례:**
1. EMR이 ServiceRequest (영상 오더) POST
2. PACS Bridge가 MWL 항목 생성
3. EMR이 ImagingStudy 업데이트 구독
4. 스터디 완료 시 PACS Bridge가 알림

---

### 5.5 기능: 메시지 큐 및 재시도

**설명:** 지속성을 통해 신뢰성 있는 메시지 전달을 보장합니다.

| 속성 | 값 |
|-----------|-------|
| **우선순위** | 필수 |
| **단계** | 2 |
| **SRS 요구사항** | SRS-ROUTE-003 |
| **PRD 요구사항** | FR-4.3.1 - FR-4.3.4 |

**사용 사례:**
1. 발신 메시지가 전달을 위해 큐잉됨
2. 첫 번째 시도 실패 (RIS 불가용)
3. 메시지가 디스크에 지속됨
4. 지수 백오프로 재시도
5. 재시도 시 전달 성공

---

## 6. 비기능 요구사항

### 6.1 성능 요구사항

| ID | 요구사항 | 목표 | 추적 |
|----|-------------|--------|-----------|
| SRS-PERF-001 | HL7 메시지 처리량 | ≥500 msg/s | NFR-1.1 |
| SRS-PERF-002 | 메시지 지연시간 (P95) | <50 ms | NFR-1.2 |
| SRS-PERF-003 | ORM에서 MWL 생성 | <100 ms | NFR-1.3 |
| SRS-PERF-004 | 동시 연결 | ≥50 | NFR-1.4 |
| SRS-PERF-005 | 메모리 기준선 | <200 MB | NFR-1.5 |
| SRS-PERF-006 | CPU 사용률 (유휴 시) | <20% | NFR-1.6 |

### 6.2 신뢰성 요구사항

| ID | 요구사항 | 목표 | 추적 |
|----|-------------|--------|-----------|
| SRS-REL-001 | 시스템 가동시간 | 99.9% | NFR-2.1 |
| SRS-REL-002 | 메시지 전달 | 100% | NFR-2.2 |
| SRS-REL-003 | 그레이스풀 저하 | 고부하 시 | NFR-2.3 |
| SRS-REL-004 | 오류 복구 | 자동 | NFR-2.4 |
| SRS-REL-005 | 큐 지속성 | 재시작 시 유지 | NFR-2.5 |

### 6.3 보안 요구사항

| ID | 요구사항 | 목표 | 추적 |
|----|-------------|--------|-----------|
| SRS-SEC-001 | TLS 지원 | TLS 1.2/1.3 | NFR-4.1, SR-1 |
| SRS-SEC-002 | 접근 로깅 | 완전 | NFR-4.2, SR-2 |
| SRS-SEC-003 | 감사 추적 | HIPAA 준수 | NFR-4.3, SR-3 |
| SRS-SEC-004 | 입력 검증 | 100% | NFR-4.4 |
| SRS-SEC-005 | 인증서 관리 | X.509 | NFR-4.5 |

### 6.4 확장성 요구사항

| ID | 요구사항 | 목표 | 추적 |
|----|-------------|--------|-----------|
| SRS-SCALE-001 | 수평 확장 | 지원 | NFR-3.1 |
| SRS-SCALE-002 | 일일 메시지량 | ≥100K | NFR-3.2 |
| SRS-SCALE-003 | MWL 항목 용량 | ≥10K | NFR-3.3 |
| SRS-SCALE-004 | 연결 풀링 | 효율적 | NFR-3.4 |

### 6.5 유지보수성 요구사항

| ID | 요구사항 | 목표 | 추적 |
|----|-------------|--------|-----------|
| SRS-MAINT-001 | 코드 커버리지 | ≥80% | NFR-5.1 |
| SRS-MAINT-002 | 문서화 | 완전 | NFR-5.2 |
| SRS-MAINT-003 | CI/CD 파이프라인 | 100% 그린 | NFR-5.3 |
| SRS-MAINT-004 | 구성 | 외부화 | NFR-5.4 |
| SRS-MAINT-005 | 로깅 | 구조화된 JSON | NFR-5.5 |

---

## 7. 요구사항 추적성 매트릭스

### 7.1 PRD에서 SRS로의 추적성

| PRD 요구사항 | SRS 요구사항 | 상태 |
|-----------------|-------------------|--------|
| FR-1.1.1-5 | SRS-HL7-001 | 명세됨 |
| FR-1.2.1 | SRS-HL7-002 | 명세됨 |
| FR-1.2.2 | SRS-HL7-003 | 명세됨 |
| FR-1.2.3 | SRS-HL7-004 | 명세됨 |
| FR-1.2.4 | SRS-HL7-005 | 명세됨 |
| FR-1.2.5 | SRS-HL7-006 | 명세됨 |
| FR-1.3.1-4 | SRS-MLLP-001, SRS-MLLP-002 | 명세됨 |
| FR-1.3.5 | SRS-MLLP-003 | 명세됨 |
| FR-2.1.1-5 | SRS-FHIR-001 | 명세됨 |
| FR-2.2.1 | SRS-FHIR-002 | 명세됨 |
| FR-2.2.2 | SRS-FHIR-003 | 명세됨 |
| FR-2.2.3 | SRS-FHIR-004 | 명세됨 |
| FR-3.1.1-6 | SRS-TRANS-001, SRS-PACS-001 | 명세됨 |
| FR-3.2.1-5 | SRS-TRANS-002, SRS-PACS-002 | 명세됨 |
| FR-4.1.1-4 | SRS-ROUTE-001 | 명세됨 |
| FR-4.2.1-4 | SRS-ROUTE-002 | 명세됨 |
| FR-4.3.1-4 | SRS-ROUTE-003 | 명세됨 |
| FR-5.1.1-4 | SRS-CFG-001 | 명세됨 |
| FR-5.2.1-4 | SRS-CFG-002 | 명세됨 |
| NFR-1.1-6 | SRS-PERF-001-006 | 명세됨 |
| NFR-2.1-5 | SRS-REL-001-005 | 명세됨 |
| NFR-3.1-4 | SRS-SCALE-001-004 | 명세됨 |
| NFR-4.1-5 | SRS-SEC-001-005 | 명세됨 |
| NFR-5.1-5 | SRS-MAINT-001-005 | 명세됨 |

### 7.2 SRS에서 테스트 케이스로의 추적성 (템플릿)

| SRS 요구사항 | 테스트 케이스 ID | 테스트 유형 | 상태 |
|-----------------|-------------|-----------|--------|
| SRS-HL7-001 | TC-HL7-001 | 단위 | 계획됨 |
| SRS-HL7-002 | TC-HL7-002 | 통합 | 계획됨 |
| SRS-HL7-003 | TC-HL7-003 | 통합 | 계획됨 |
| SRS-HL7-004 | TC-HL7-004 | 통합 | 계획됨 |
| SRS-HL7-005 | TC-HL7-005 | 통합 | 계획됨 |
| SRS-HL7-006 | TC-HL7-006 | 단위 | 계획됨 |
| SRS-MLLP-001 | TC-MLLP-001 | 통합 | 계획됨 |
| SRS-MLLP-002 | TC-MLLP-002 | 통합 | 계획됨 |
| SRS-MLLP-003 | TC-MLLP-003 | 통합 | 계획됨 |
| SRS-FHIR-001 | TC-FHIR-001 | 통합 | 계획됨 |
| SRS-TRANS-001 | TC-TRANS-001 | 단위 | 계획됨 |
| SRS-TRANS-002 | TC-TRANS-002 | 단위 | 계획됨 |
| SRS-ROUTE-001 | TC-ROUTE-001 | 단위 | 계획됨 |
| SRS-ROUTE-002 | TC-ROUTE-002 | 통합 | 계획됨 |
| SRS-ROUTE-003 | TC-ROUTE-003 | 통합 | 계획됨 |
| SRS-PACS-001 | TC-PACS-001 | 통합 | 계획됨 |
| SRS-PACS-002 | TC-PACS-002 | 통합 | 계획됨 |

### 7.3 교차 참조 요약

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    요구사항 추적성 개요                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   PRD                    SRS                      테스트 케이스          │
│   ───                    ───                      ─────────             │
│                                                                          │
│   FR-1.x  ────────────►  SRS-HL7-xxx   ────────►  TC-HL7-xxx           │
│   (HL7 게이트웨이)       (HL7 모듈)               (HL7 테스트)           │
│                                                                          │
│   FR-1.3.x ───────────►  SRS-MLLP-xxx  ────────►  TC-MLLP-xxx          │
│   (MLLP 전송)            (MLLP 모듈)              (전송 테스트)          │
│                                                                          │
│   FR-2.x  ────────────►  SRS-FHIR-xxx  ────────►  TC-FHIR-xxx          │
│   (FHIR 게이트웨이)      (FHIR 모듈)              (FHIR 테스트)          │
│                                                                          │
│   FR-3.x  ────────────►  SRS-TRANS-xxx ────────►  TC-TRANS-xxx         │
│   (변환)                  SRS-PACS-xxx             (매핑 테스트)          │
│                                                                          │
│   FR-4.x  ────────────►  SRS-ROUTE-xxx ────────►  TC-ROUTE-xxx         │
│   (라우팅)               (라우팅 모듈)            (라우팅 테스트)        │
│                                                                          │
│   FR-5.x  ────────────►  SRS-CFG-xxx   ────────►  TC-CFG-xxx           │
│   (구성)                  (구성 모듈)              (구성 테스트)          │
│                                                                          │
│   NFR-x   ────────────►  SRS-PERF-xxx  ────────►  TC-PERF-xxx          │
│   (비기능)               SRS-REL-xxx              (성능)                 │
│                          SRS-SEC-xxx              (부하 테스트)          │
│                          SRS-SCALE-xxx                                  │
│                          SRS-MAINT-xxx                                  │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 8. 부록

### 부록 A: 요구사항 ID 체계

| 접두사 | 카테고리 | 예시 |
|--------|----------|---------|
| SRS-HL7-xxx | HL7 게이트웨이 모듈 | SRS-HL7-001 |
| SRS-MLLP-xxx | MLLP 전송 | SRS-MLLP-001 |
| SRS-FHIR-xxx | FHIR 게이트웨이 모듈 | SRS-FHIR-001 |
| SRS-TRANS-xxx | 변환/매핑 | SRS-TRANS-001 |
| SRS-ROUTE-xxx | 메시지 라우팅 | SRS-ROUTE-001 |
| SRS-PACS-xxx | pacs_system 통합 | SRS-PACS-001 |
| SRS-CFG-xxx | 구성 | SRS-CFG-001 |
| SRS-PERF-xxx | 성능 | SRS-PERF-001 |
| SRS-REL-xxx | 신뢰성 | SRS-REL-001 |
| SRS-SEC-xxx | 보안 | SRS-SEC-001 |
| SRS-SCALE-xxx | 확장성 | SRS-SCALE-001 |
| SRS-MAINT-xxx | 유지보수성 | SRS-MAINT-001 |

### 부록 B: 오류 코드 레지스트리

```
pacs_bridge 오류 코드: -900 ~ -999

HL7 파싱 오류 (-900 ~ -919):
  -900: INVALID_HL7_MESSAGE
  -901: MISSING_MSH_SEGMENT
  -902: INVALID_SEGMENT_STRUCTURE
  -903: MISSING_REQUIRED_FIELD
  -904: INVALID_FIELD_VALUE
  -905: UNKNOWN_MESSAGE_TYPE

MLLP 전송 오류 (-920 ~ -939):
  -920: MLLP_CONNECTION_FAILED
  -921: MLLP_SEND_FAILED
  -922: MLLP_RECEIVE_TIMEOUT
  -923: MLLP_INVALID_FRAME
  -924: MLLP_TLS_HANDSHAKE_FAILED

변환 오류 (-940 ~ -959):
  -940: MAPPING_FAILED
  -941: MISSING_MAPPING_CONFIG
  -942: INVALID_CODE_SYSTEM
  -943: PATIENT_NOT_FOUND
  -944: ORDER_NOT_FOUND

큐 오류 (-960 ~ -979):
  -960: QUEUE_FULL
  -961: MESSAGE_EXPIRED
  -962: DELIVERY_FAILED
  -963: RETRY_LIMIT_EXCEEDED

pacs_system 통합 오류 (-980 ~ -999):
  -980: PACS_CONNECTION_FAILED
  -981: MWL_UPDATE_FAILED
  -982: MPPS_HANDLER_ERROR
  -983: DICOM_TRANSLATION_ERROR
```

### 부록 C: IHE SWF 트랜잭션 매핑

| IHE 트랜잭션 | PACS Bridge 역할 | 구현 |
|-----------------|------------------|----------------|
| RAD-2 (Placer Order) | 수신자 | SRS-HL7-003 (ORM 처리) |
| RAD-3 (Filler Order) | 발신자 | SRS-TRANS-002 (MPPS→ORM) |
| RAD-4 (Procedure Scheduled) | 발신자 | SRS-PACS-001 (MWL 생성) |
| RAD-6 (MPPS In Progress) | 수신자 | SRS-PACS-002 (MPPS 핸들러) |
| RAD-7 (MPPS Completed) | 수신자 | SRS-PACS-002 (MPPS 핸들러) |

### 부록 D: 개정 이력

| 버전 | 날짜 | 작성자 | 변경사항 |
|---------|------|--------|---------|
| 1.0.0 | 2025-12-07 | kcenon | 최초 버전 |

### 부록 E: 용어집

| 용어 | 정의 |
|------|------------|
| Accession Number | 영상 오더/스터디의 고유 식별자 |
| MLLP Frame | VT 접두사와 FS/CR 접미사가 있는 메시지 봉투 |
| Placer Order | 요청 시스템(HIS)이 생성한 오더 |
| Filler Order | 수행 시스템(RIS)이 수락한 오더 |
| Presentation Context | DICOM의 메시지 구문에 대한 합의 |
| Worklist Entry | 모달리티 쿼리를 위한 예정된 프로시저 항목 |

---

*문서 버전: 0.2.0.0*
*작성일: 2025-12-07*
*작성자: kcenon@naver.com*
