# SDS - 요구사항 추적성 매트릭스

> **버전:** 0.2.0.0
> **상위 문서:** [SDS_KO.md](SDS_KO.md)
> **최종 수정일:** 2026-02-08

---

## 목차

- [1. 개요](#1-개요)
- [2. 기능 요구사항 추적성](#2-기능-요구사항-추적성)
- [3. 비기능 요구사항 추적성](#3-비기능-요구사항-추적성)
- [4. 설계 요소 인덱스](#4-설계-요소-인덱스)
- [5. 테스트 케이스 매핑](#5-테스트-케이스-매핑)
- [6. 갭 분석](#6-갭-분석)

---

## 1. 개요

### 1.1 목적

이 문서는 다음 항목 간의 양방향 추적성을 수립합니다:
- **SRS 요구사항** → **설계 요소** → **테스트 케이스**

### 1.2 추적성 범례

| 기호 | 의미 |
|------|------|
| ✓ | 완전히 추적되고 구현됨 |
| ◐ | 부분적으로 추적됨 |
| ○ | 계획되었으나 아직 설계되지 않음 |
| - | 해당 없음 |

### 1.3 문서 참조

| 문서 | 설명 |
|------|------|
| SRS.md | 소프트웨어 요구사항 명세서 |
| SDS.md | 소프트웨어 설계 명세서 (메인) |
| SDS_COMPONENTS.md | 컴포넌트 설계 |
| SDS_INTERFACES.md | 인터페이스 명세 |
| SDS_SEQUENCES.md | 시퀀스 다이어그램 |

---

## 2. 기능 요구사항 추적성

### 2.1 FR-1: HL7 v2.x 게이트웨이

| 요구사항 ID | 요구사항 | 설계 요소 | 시퀀스 | 상태 |
|------------|---------|----------|--------|------|
| FR-1.1.1 | HL7 v2.x 메시지 파싱 (v2.3.1-v2.9) | DES-HL7-002 (hl7_parser) | SEQ-001 | ✓ |
| FR-1.1.2 | 표준 구분자 및 이스케이프 시퀀스 지원 | DES-HL7-001 (hl7_delimiters) | SEQ-001 | ✓ |
| FR-1.1.3 | 반복 필드 및 컴포넌트 처리 | DES-HL7-001 (hl7_field) | SEQ-001 | ✓ |
| FR-1.1.4 | 스키마에 대한 메시지 구조 검증 | DES-HL7-004 (hl7_validator) | - | ✓ |
| FR-1.1.5 | Z-세그먼트 (사용자 정의 세그먼트) 지원 | DES-HL7-002 (hl7_parser) | SEQ-003 | ✓ |
| FR-1.2.1 | ADT^A01, A04, A08, A40 처리 | DES-HL7-004, INT-MOD-001 | SEQ-002 | ✓ |
| FR-1.2.2 | ORM^O01 처리 | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-1.2.3 | ORU^R01 생성 | DES-HL7-003 (hl7_builder) | - | ✓ |
| FR-1.2.4 | SIU^S12-S15 처리 | DES-HL7-004 | - | ○ |
| FR-1.2.5 | ACK 응답 생성 | DES-HL7-003, INT-API-002 | SEQ-001 | ✓ |
| FR-1.3.1 | MLLP 서버 (리스너) 구현 | DES-MLLP-001 | SEQ-001 | ✓ |
| FR-1.3.2 | MLLP 클라이언트 (송신자) 구현 | DES-MLLP-002 | SEQ-005 | ✓ |
| FR-1.3.3 | 영속적 및 일시적 연결 지원 | DES-MLLP-001, DES-MLLP-002 | - | ✓ |
| FR-1.3.4 | 메시지 프레이밍 처리 (VT, FS, CR) | DES-MLLP-003 | SEQ-001 | ✓ |
| FR-1.3.5 | TLS를 통한 MLLP 지원 | DES-MLLP-001, DES-MLLP-002 | - | ○ |

### 2.2 FR-2: FHIR R4 게이트웨이

| 요구사항 ID | 요구사항 | 설계 요소 | 시퀀스 | 상태 |
|------------|---------|----------|--------|------|
| FR-2.1.1 | FHIR REST 서버 구현 | DES-FHIR-001 | SEQ-007 | ○ |
| FR-2.1.2 | 리소스에 대한 CRUD 연산 지원 | DES-FHIR-001 | SEQ-007, SEQ-008 | ○ |
| FR-2.1.3 | 검색 매개변수 구현 | DES-FHIR-001, INT-EXT-002 | SEQ-008 | ○ |
| FR-2.1.4 | JSON 및 XML 형식 지원 | DES-FHIR-002 | - | ○ |
| FR-2.1.5 | 대용량 결과 집합에 대한 페이지네이션 처리 | DES-FHIR-001 | - | ○ |
| FR-2.2.1 | Patient 리소스 지원 | DES-FHIR-002 (patient_resource) | - | ○ |
| FR-2.2.2 | ServiceRequest (영상 오더) 지원 | DES-FHIR-002 (service_request_resource) | SEQ-007 | ○ |
| FR-2.2.3 | ImagingStudy 리소스 지원 | DES-FHIR-002 (imaging_study_resource) | SEQ-008 | ✓ |
| FR-2.2.4 | DiagnosticReport 리소스 지원 | DES-FHIR-002 (diagnostic_report_resource) | - | ○ |
| FR-2.2.5 | Task (워크리스트 항목) 지원 | DES-FHIR-002 | - | ○ |
| FR-2.3.1 | REST-hook 구독 지원 | - | - | ○ |
| FR-2.3.2 | 검사 가용성 알림 | - | - | ○ |
| FR-2.3.3 | 리포트 완료 알림 | - | - | ○ |

### 2.3 FR-3: DICOM 통합

| 요구사항 ID | 요구사항 | 설계 요소 | 시퀀스 | 상태 |
|------------|---------|----------|--------|------|
| FR-3.1.1 | HL7 ORM을 DICOM MWL 항목으로 변환 | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-3.1.2 | 환자 인구통계 매핑 (PID → Patient 모듈) | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-3.1.3 | 오더 정보 매핑 (ORC/OBR → SPS) | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-3.1.4 | Study Instance UID 사전 할당 지원 (ZDS) | DES-TRANS-001 | SEQ-003 | ✓ |
| FR-3.1.5 | 오더 취소 처리 (ORC-1=CA) | DES-TRANS-001, DES-PACS-001 | SEQ-004 | ✓ |
| FR-3.1.6 | 오더 수정 처리 (ORC-1=XO) | DES-TRANS-001, DES-PACS-001 | - | ○ |
| FR-3.2.1 | MPPS N-CREATE 알림 수신 | DES-PACS-002 | SEQ-005 | ✓ |
| FR-3.2.2 | MPPS IN PROGRESS를 HL7 ORM 상태로 변환 | DES-TRANS-002 | SEQ-005 | ✓ |
| FR-3.2.3 | MPPS N-SET (COMPLETED) 알림 수신 | DES-PACS-002 | SEQ-006 | ✓ |
| FR-3.2.4 | MPPS COMPLETED를 HL7 ORM 상태로 변환 | DES-TRANS-002 | SEQ-006 | ✓ |
| FR-3.2.5 | MPPS DISCONTINUED 처리 | DES-TRANS-002 | - | ○ |
| FR-3.3.1 | PACS에서 리포트 상태 수신 | - | - | ○ |
| FR-3.3.2 | 예비 리포트에 대한 ORU^R01 생성 | DES-HL7-003, DES-TRANS-002 | - | ○ |
| FR-3.3.3 | 최종 리포트에 대한 ORU^R01 생성 | DES-HL7-003, DES-TRANS-002 | - | ○ |
| FR-3.3.4 | 리포트 수정 지원 | - | - | ○ |

### 2.4 FR-4: 메시지 라우팅

| 요구사항 ID | 요구사항 | 설계 요소 | 시퀀스 | 상태 |
|------------|---------|----------|--------|------|
| FR-4.1.1 | ADT 메시지를 환자 캐시로 라우팅 | DES-ROUTE-001, DES-PACS-003 | SEQ-002 | ✓ |
| FR-4.1.2 | ORM 메시지를 MWL 관리자로 라우팅 | DES-ROUTE-001 | SEQ-003 | ✓ |
| FR-4.1.3 | 메시지 타입 및 트리거 기반 라우팅 | DES-ROUTE-001 | SEQ-001 | ✓ |
| FR-4.1.4 | 조건부 라우팅 규칙 지원 | DES-ROUTE-001 | - | ○ |
| FR-4.2.1 | MPPS 알림을 RIS로 라우팅 | DES-ROUTE-001 | SEQ-005, SEQ-006 | ✓ |
| FR-4.2.2 | ORU 메시지를 설정된 엔드포인트로 라우팅 | DES-ROUTE-001 | - | ○ |
| FR-4.2.3 | 다중 목적지 라우팅 지원 | DES-ROUTE-001 | - | ○ |
| FR-4.2.4 | 페일오버 라우팅 구현 | DES-ROUTE-001 | - | ○ |
| FR-4.3.1 | 신뢰성 있는 전달을 위한 아웃바운드 메시지 큐잉 | DES-ROUTE-002 | SEQ-009 | ✓ |
| FR-4.3.2 | 지수 백오프를 통한 재시도 구현 | DES-ROUTE-002 | SEQ-009 | ✓ |
| FR-4.3.3 | 메시지 우선순위 지원 | DES-ROUTE-002 | SEQ-009 | ✓ |
| FR-4.3.4 | 장애 복구를 위한 큐 영속화 | DES-ROUTE-002 | SEQ-009 | ✓ |

### 2.5 FR-5: 설정 관리

| 요구사항 ID | 요구사항 | 설계 요소 | 시퀀스 | 상태 |
|------------|---------|----------|--------|------|
| FR-5.1.1 | HL7 리스너 포트 설정 | DES-CFG-001, INT-CFG-001 | - | ✓ |
| FR-5.1.2 | 아웃바운드 HL7 목적지 설정 | DES-CFG-001, INT-CFG-001 | - | ✓ |
| FR-5.1.3 | pacs_system 연결 설정 | DES-CFG-001, INT-CFG-001 | - | ✓ |
| FR-5.1.4 | 설정 핫 리로드 지원 | DES-CFG-001, INT-API-001 | - | ○ |
| FR-5.2.1 | 모달리티-AE 타이틀 매핑 설정 | DES-CFG-001, INT-CFG-001 | - | ✓ |
| FR-5.2.2 | 검사 코드 매핑 설정 | DES-CFG-001, DES-TRANS-001 | - | ○ |
| FR-5.2.3 | 환자 ID 도메인 매핑 설정 | DES-CFG-001 | - | ○ |
| FR-5.2.4 | 사용자 정의 필드 매핑 지원 | DES-CFG-001 | - | ○ |

---

## 3. 비기능 요구사항 추적성

### 3.1 NFR-1: 성능

| 요구사항 ID | 요구사항 | 설계 요소 | 구현 전략 | 상태 |
|------------|---------|----------|----------|------|
| NFR-1.1 | 메시지 처리량 ≥500 msg/s | DES-MLLP-001, DES-ROUTE-002 | 스레드 풀, 비동기 I/O | ✓ |
| NFR-1.2 | 메시지 지연 시간 (P95) <50 ms | DES-ROUTE-001 | 락-프리 라우팅 | ✓ |
| NFR-1.3 | MWL 조회 응답 <100 ms | DES-PACS-001 | 연결 풀링 | ✓ |
| NFR-1.4 | 동시 연결 ≥50 | DES-MLLP-001 | 연결당 스레드 | ✓ |
| NFR-1.5 | 메모리 기준선 <200 MB | DES-PACS-003 | 제한된 캐시 | ✓ |
| NFR-1.6 | CPU 사용률 <20% (유휴 시) | 전체 | 효율적인 이벤트 루프 | ✓ |

### 3.2 NFR-2: 신뢰성

| 요구사항 ID | 요구사항 | 설계 요소 | 구현 전략 | 상태 |
|------------|---------|----------|----------|------|
| NFR-2.1 | 시스템 가용성 99.9% | INT-API-001 | 상태 확인, 자동 재시작 | ○ |
| NFR-2.2 | 메시지 전달 100% | DES-ROUTE-002 | 영속적 큐, 재시도 | ✓ |
| NFR-2.3 | 점진적 저하 | INT-API-001 | 큐 버퍼링 | ✓ |
| NFR-2.4 | 오류 복구 (자동) | DES-ROUTE-002 | 지수 백오프 | ✓ |
| NFR-2.5 | 큐 영속성 | DES-ROUTE-002 | SQLite 저장 | ✓ |

### 3.3 NFR-3: 확장성

| 요구사항 ID | 요구사항 | 설계 요소 | 구현 전략 | 상태 |
|------------|---------|----------|----------|------|
| NFR-3.1 | 수평적 확장 | INT-API-001 | 무상태 설계 | ○ |
| NFR-3.2 | 일일 메시지 볼륨 ≥100K | DES-ROUTE-002 | 큐 용량 | ✓ |
| NFR-3.3 | MWL 항목 용량 ≥10K | DES-PACS-001 | 데이터베이스 인덱싱 | ✓ |
| NFR-3.4 | 연결 풀링 | DES-PACS-001, DES-MLLP-002 | 풀 관리 | ✓ |

### 3.4 NFR-4: 보안

| 요구사항 ID | 요구사항 | 설계 요소 | 구현 전략 | 상태 |
|------------|---------|----------|----------|------|
| NFR-4.1 | TLS 지원 (TLS 1.2/1.3) | DES-MLLP-001, DES-FHIR-001 | network_system TLS | ○ |
| NFR-4.2 | 접근 로깅 (완전) | INT-ECO-002 | 구조화된 로깅 | ✓ |
| NFR-4.3 | 감사 추적 (HIPAA 준수) | INT-ECO-002 | 감사 파일 | ✓ |
| NFR-4.4 | 입력 검증 (100%) | DES-HL7-004 | 스키마 검증 | ✓ |
| NFR-4.5 | 인증서 관리 (X.509) | DES-MLLP-001 | TLS 설정 | ○ |

### 3.5 NFR-5: 유지보수성

| 요구사항 ID | 요구사항 | 설계 요소 | 구현 전략 | 상태 |
|------------|---------|----------|----------|------|
| NFR-5.1 | 코드 커버리지 ≥80% | 전체 | 단위 테스트 | ○ |
| NFR-5.2 | 문서화 (완전) | SDS 문서 세트 | API 문서, 예제 | ✓ |
| NFR-5.3 | CI/CD 파이프라인 (100% 녹색) | - | GitHub Actions | ○ |
| NFR-5.4 | 설정 (외부화) | DES-CFG-001 | YAML/JSON 파일 | ✓ |
| NFR-5.5 | 로깅 (구조화) | INT-ECO-002 | JSON 형식 | ✓ |

---

## 4. 설계 요소 인덱스

### 4.1 컴포넌트 설계

| 설계 ID | 컴포넌트 | 모듈 | SRS 참조 |
|---------|---------|------|---------|
| DES-HL7-001 | hl7_message | HL7 게이트웨이 | FR-1.1.1, FR-1.1.2, FR-1.1.3 |
| DES-HL7-002 | hl7_parser | HL7 게이트웨이 | FR-1.1.1, FR-1.1.5 |
| DES-HL7-003 | hl7_builder | HL7 게이트웨이 | FR-1.2.3, FR-1.2.5 |
| DES-HL7-004 | hl7_validator | HL7 게이트웨이 | FR-1.1.4, FR-1.2 |
| DES-MLLP-001 | mllp_server | MLLP 전송 | FR-1.3.1, FR-1.3.3, FR-1.3.4 |
| DES-MLLP-002 | mllp_client | MLLP 전송 | FR-1.3.2, FR-1.3.3 |
| DES-MLLP-003 | mllp_connection | MLLP 전송 | FR-1.3.4 |
| DES-FHIR-001 | fhir_server | FHIR 게이트웨이 | FR-2.1.1, FR-2.1.2, FR-2.1.3 |
| DES-FHIR-002 | fhir_resource | FHIR 게이트웨이 | FR-2.2.1 - FR-2.2.5 |
| DES-TRANS-001 | hl7_dicom_mapper | 변환 | FR-3.1.1 - FR-3.1.6 |
| DES-TRANS-002 | dicom_hl7_mapper | 변환 | FR-3.2.1 - FR-3.2.5 |
| DES-TRANS-003 | fhir_dicom_mapper | 변환 | FR-2.2.2, FR-2.2.3 |
| DES-ROUTE-001 | message_router | 라우팅 | FR-4.1, FR-4.2 |
| DES-ROUTE-002 | queue_manager | 라우팅 | FR-4.3 |
| DES-PACS-001 | mwl_client | PACS 어댑터 | IR-1, FR-3.1 |
| DES-PACS-002 | mpps_handler | PACS 어댑터 | IR-1, FR-3.2 |
| DES-PACS-003 | patient_cache | PACS 어댑터 | FR-4.1.1 |
| DES-CFG-001 | bridge_config | 설정 | FR-5.1, FR-5.2 |

### 4.2 인터페이스 명세

| 인터페이스 ID | 이름 | 타입 | SRS 참조 |
|--------------|------|------|---------|
| INT-EXT-001 | HL7/MLLP 인터페이스 | 외부 | FR-1.3, PCR-1 |
| INT-EXT-002 | FHIR REST 인터페이스 | 외부 | FR-2.1, PCR-2 |
| INT-EXT-003 | pacs_system DICOM 인터페이스 | 외부 | IR-1 |
| INT-API-001 | bridge_server | 공개 API | - |
| INT-API-002 | hl7_gateway | 공개 API | FR-1 |
| INT-MOD-001 | hl7_message 접근 | 내부 | FR-1.1 |
| INT-MOD-002 | 매퍼 인터페이스 | 내부 | FR-3 |
| INT-ECO-001 | Result<T> 패턴 | 통합 | - |
| INT-ECO-002 | 로거 통합 | 통합 | NFR-4.2, NFR-5.5 |
| INT-ERR-001 | 오류 코드 | 오류 처리 | 부록 C |
| INT-CFG-001 | 설정 파일 | 설정 | FR-5, 부록 D |

### 4.3 시퀀스 다이어그램

| 시퀀스 ID | 이름 | SRS 참조 |
|----------|------|---------|
| SEQ-001 | 인바운드 HL7 메시지 흐름 | FR-1.1, FR-1.2, FR-1.3 |
| SEQ-002 | ADT 메시지 처리 | FR-4.1.1 |
| SEQ-003 | ORM → MWL 항목 생성 | FR-3.1.1 - FR-3.1.4 |
| SEQ-004 | 오더 취소 | FR-3.1.5 |
| SEQ-005 | MPPS 진행 중 → HL7 | FR-3.2.1, FR-3.2.2 |
| SEQ-006 | MPPS 완료 → HL7 | FR-3.2.3, FR-3.2.4 |
| SEQ-007 | FHIR ServiceRequest 생성 | FR-2.1.3, FR-2.2.2 |
| SEQ-008 | FHIR ImagingStudy 조회 | FR-2.1.2, FR-2.2.3 |
| SEQ-009 | 메시지 큐 및 재시도 | FR-4.3.1, FR-4.3.2 |
| SEQ-010 | Dead Letter 처리 | FR-4.3.2, NFR-2.2 |
| SEQ-011 | 유효하지 않은 HL7 메시지 처리 | 오류 처리 |
| SEQ-012 | pacs_system 연결 실패 | 오류 처리 |
| SEQ-013 | 완전한 IHE SWF 시퀀스 | PCR-3 |

---

## 5. 테스트 케이스 매핑

### 5.1 단위 테스트 커버리지

| 설계 ID | 컴포넌트 | 테스트 파일 | 테스트 수 (추정) |
|---------|---------|------------|----------------|
| DES-HL7-001 | hl7_message | tests/hl7/hl7_message_test.cpp | 15 |
| DES-HL7-002 | hl7_parser | tests/hl7/hl7_parser_test.cpp | 20 |
| DES-HL7-003 | hl7_builder | tests/hl7/hl7_builder_test.cpp | 15 |
| DES-HL7-004 | hl7_validator | tests/hl7/hl7_validator_test.cpp | 12 |
| DES-MLLP-001 | mllp_server | tests/mllp/mllp_server_test.cpp | 10 |
| DES-MLLP-002 | mllp_client | tests/mllp/mllp_client_test.cpp | 10 |
| DES-MLLP-003 | mllp_connection | tests/mllp/mllp_frame_test.cpp | 8 |
| DES-TRANS-001 | hl7_dicom_mapper | tests/mapping/hl7_dicom_mapper_test.cpp | 20 |
| DES-TRANS-002 | dicom_hl7_mapper | tests/dicom_hl7_mapper_test.cpp | 17 |
| DES-ROUTE-001 | message_router | tests/router/message_router_test.cpp | 10 |
| DES-ROUTE-002 | queue_manager | tests/router/queue_manager_test.cpp | 15 |
| DES-PACS-001 | mwl_client | tests/pacs/mwl_client_test.cpp | 10 |
| DES-PACS-002 | mpps_handler | tests/pacs/mpps_handler_test.cpp | 8 |
| DES-PACS-003 | patient_cache | tests/pacs/patient_cache_test.cpp | 10 |
| DES-CFG-001 | bridge_config | tests/config/bridge_config_test.cpp | 10 |

**총 추정 단위 테스트:** ~180

### 5.2 통합 테스트 시나리오

| 시나리오 ID | 설명 | 대상 요구사항 |
|------------|------|-------------|
| IT-001 | 종단간 ORM에서 MWL 흐름 | FR-1.3, FR-3.1, FR-4.1 |
| IT-002 | MPPS 알림에서 HL7 | FR-3.2, FR-4.2 |
| IT-003 | 큐 재시도 및 복구 | FR-4.3, NFR-2.2 |
| IT-004 | 다중 동시 연결 | NFR-1.4 |
| IT-005 | 처리량 벤치마크 (500 msg/s) | NFR-1.1 |
| IT-006 | 환자 캐시 동기화 | FR-4.1.1 |
| IT-007 | 오더 취소 워크플로우 | FR-3.1.5 |
| IT-008 | 오류 처리 및 NAK | 오류 처리 |

### 5.3 적합성 테스트

| 테스트 ID | 표준 | 설명 | 요구사항 |
|----------|------|------|---------|
| CT-001 | HL7 v2.5.1 | ADT 메시지 적합성 | PCR-1 |
| CT-002 | HL7 v2.5.1 | ORM 메시지 적합성 | PCR-1 |
| CT-003 | HL7 v2.5.1 | ORU 메시지 적합성 | PCR-1 |
| CT-004 | MLLP | 프레임 처리 | FR-1.3.4 |
| CT-005 | IHE SWF | RAD-2/4 트랜잭션 | PCR-3 |
| CT-006 | IHE SWF | RAD-6/7 트랜잭션 | PCR-3 |
| CT-007 | FHIR R4 | 리소스 검증 | PCR-2 |

---

## 6. 갭 분석

### 6.1 아직 설계되지 않은 요구사항

| 요구사항 ID | 설명 | 우선순위 | 대상 단계 |
|------------|------|---------|----------|
| FR-1.2.4 | SIU^S12-S15 처리 | Should Have | 2단계 |
| FR-1.3.5 | TLS를 통한 MLLP | Should Have | 2단계 |
| FR-2.x | FHIR 게이트웨이 (전체) | Should Have | 3단계 |
| FR-3.1.6 | 오더 수정 | Should Have | 2단계 |
| FR-3.2.5 | MPPS DISCONTINUED | Should Have | 2단계 |
| FR-3.3.x | 리포트 통합 | Should Have | 3단계 |
| FR-4.1.4 | 조건부 라우팅 | Should Have | 2단계 |
| FR-4.2.2-4 | 고급 라우팅 | Should Have | 3단계 |
| FR-5.1.4 | 핫 리로드 설정 | Should Have | 3단계 |
| FR-5.2.2-4 | 고급 매핑 | Could Have | 3단계 |

### 6.2 구현 대기 중인 설계 요소

| 설계 ID | 컴포넌트 | 상태 | 블로커 |
|---------|---------|------|-------|
| DES-FHIR-001 | fhir_server | 계획됨 | 3단계 |
| DES-FHIR-002 | fhir_resource | 계획됨 | 3단계 |
| DES-TRANS-003 | fhir_dicom_mapper | 계획됨 | 3단계 |

### 6.3 커버리지 요약

| 카테고리 | 전체 | 설계됨 | 구현됨 | 커버리지 |
|---------|------|-------|-------|---------|
| FR-1 (HL7 게이트웨이) | 15 | 14 | 0 | 93% 설계됨 |
| FR-2 (FHIR 게이트웨이) | 13 | 13 | 0 | 100% 설계됨 (3단계) |
| FR-3 (DICOM 통합) | 14 | 12 | 0 | 86% 설계됨 |
| FR-4 (메시지 라우팅) | 12 | 10 | 0 | 83% 설계됨 |
| FR-5 (설정) | 8 | 6 | 0 | 75% 설계됨 |
| **합계** | **62** | **55** | **0** | **89% 설계됨** |

### 6.4 권장 조치

1. **1단계 우선순위:**
   - 모든 "Must Have" 요구사항 구현 완료
   - 핵심 HL7/MLLP 및 ORM→MWL 흐름에 집중
   - 포괄적인 단위 테스트 스위트 구축

2. **2단계 추가 사항:**
   - SIU 메시지 지원 추가 (일정 관리)
   - MLLP용 TLS 구현
   - 오더 수정 처리 추가
   - 라우팅 기능 강화

3. **3단계 로드맵:**
   - 완전한 FHIR R4 게이트웨이 구현
   - 리포트 통합 (ORU 생성)
   - 핫 리로드 설정
   - 고급 매핑 기능

---

*문서 버전: 0.2.0.0*
*작성일: 2025-12-07*
*작성자: kcenon@naver.com*
