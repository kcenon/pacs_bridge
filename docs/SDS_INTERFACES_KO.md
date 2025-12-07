# SDS - 인터페이스 명세

> **버전:** 1.0.0
> **상위 문서:** [SDS_KO.md](SDS_KO.md)
> **최종 수정일:** 2025-12-07

---

## 목차

- [1. 개요](#1-개요)
- [2. 외부 시스템 인터페이스](#2-외부-시스템-인터페이스)
- [3. 공개 API 인터페이스](#3-공개-api-인터페이스)
- [4. 내부 모듈 인터페이스](#4-내부-모듈-인터페이스)
- [5. 에코시스템 통합 인터페이스](#5-에코시스템-통합-인터페이스)
- [6. 오류 처리 인터페이스](#6-오류-처리-인터페이스)
- [7. 설정 인터페이스](#7-설정-인터페이스)

---

## 1. 개요

### 1.1 인터페이스 아키텍처

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          인터페이스 아키텍처                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                      외부 인터페이스                                 │   │
│   │                                                                      │   │
│   │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────┐   │   │
│   │  │ HIS / RIS    │  │ EMR / EHR    │  │ pacs_system              │   │   │
│   │  │ (HL7 v2.x)   │  │ (FHIR R4)    │  │ (DICOM)                  │   │   │
│   │  └──────────────┘  └──────────────┘  └──────────────────────────┘   │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                      │                                       │
│                                      ▼                                       │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                        공개 API 계층                                 │   │
│   │                                                                      │   │
│   │  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐             │   │
│   │  │ mllp_server   │ │ fhir_server   │ │ bridge_server │             │   │
│   │  │ (포트 2575)   │ │ (포트 8080)   │ │ (오케스트레이터)│             │   │
│   │  └───────────────┘ └───────────────┘ └───────────────┘             │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                      │                                       │
│                                      ▼                                       │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                      내부 모듈 인터페이스                            │   │
│   │                                                                      │   │
│   │  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐             │   │
│   │  │  hl7_parser   │ │  hl7_builder  │ │ message_router│             │   │
│   │  └───────────────┘ └───────────────┘ └───────────────┘             │   │
│   │  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐             │   │
│   │  │ hl7_dicom_    │ │ dicom_hl7_    │ │ queue_manager │             │   │
│   │  │ mapper        │ │ mapper        │ │               │             │   │
│   │  └───────────────┘ └───────────────┘ └───────────────┘             │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                      │                                       │
│                                      ▼                                       │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │                    에코시스템 통합 계층                              │   │
│   │                                                                      │   │
│   │  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐             │   │
│   │  │ network_      │ │ thread_       │ │ logger_       │             │   │
│   │  │ adapter       │ │ adapter       │ │ adapter       │             │   │
│   │  └───────────────┘ └───────────────┘ └───────────────┘             │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 외부 시스템 인터페이스

### INT-EXT-001: HL7 v2.x / MLLP 인터페이스

**추적 대상:** FR-1.3, PCR-1

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      HL7 v2.x / MLLP 프로토콜 인터페이스                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  프로토콜: HL7 v2.x over MLLP                                               │
│  전송: TCP/IP (TLS 선택사항)                                                │
│  기본 포트: 2575                                                             │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ MLLP 프레임 구조                                                         ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  ┌────┬─────────────────────────────────────────────────────────┬────┐  ││
│  │  │ VT │                    HL7 메시지                           │FS CR│ ││
│  │  │0x0B│                    (UTF-8)                              │0x1C│  ││
│  │  │    │                                                         │0x0D│  ││
│  │  └────┴─────────────────────────────────────────────────────────┴────┘  ││
│  │                                                                          ││
│  │  VT (0x0B) = 메시지 블록 시작                                           ││
│  │  FS (0x1C) = 메시지 블록 종료                                           ││
│  │  CR (0x0D) = 캐리지 리턴 (블록 종료자)                                  ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ 지원 메시지 타입                                                         ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  인바운드 (HIS/RIS → 브릿지):                                           ││
│  │    • ADT^A01  환자 입원                                                 ││
│  │    • ADT^A04  환자 등록                                                 ││
│  │    • ADT^A08  환자 정보 업데이트                                        ││
│  │    • ADT^A40  환자 병합                                                 ││
│  │    • ORM^O01  오더 메시지                                               ││
│  │    • SIU^S12  예약 요청                                                 ││
│  │    • SIU^S13  예약 알림                                                 ││
│  │    • SIU^S14  예약 수정                                                 ││
│  │    • SIU^S15  예약 취소                                                 ││
│  │                                                                          ││
│  │  아웃바운드 (브릿지 → RIS):                                             ││
│  │    • ACK     응답 확인                                                  ││
│  │    • ORM^O01 오더 상태 업데이트                                         ││
│  │    • ORU^R01 검사 결과                                                  ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ HL7 메시지 구조                                                          ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  MSH|^~\&|SENDING_APP|SENDING_FAC|RECEIVING_APP|RECEIVING_FAC|...      ││
│  │  PID|||123456^^^ISSUER||DOE^JOHN^||19800101|M|||...                     ││
│  │  PV1|1|O|...                                                            ││
│  │  ORC|NW|12345^HIS|||SC||||...                                           ││
│  │  OBR|1|12345^HIS||CT^CT CHEST^LOCAL|...                                 ││
│  │  ZDS|1.2.840.113619.2.55.1234567890.123456|                             ││
│  │                                                                          ││
│  │  세그먼트 구분자: CR (0x0D)                                             ││
│  │  필드 구분자: | (MSH-1)                                                 ││
│  │  인코딩 문자: ^~\& (MSH-2)                                              ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ 응답 확인 코드                                                           ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  AA = Application Accept (메시지 정상 처리됨)                           ││
│  │  AE = Application Error (메시지 거부, 메시지 오류)                      ││
│  │  AR = Application Reject (메시지 거부, 메시지 오류 아님)                ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### INT-EXT-002: FHIR R4 REST 인터페이스

**추적 대상:** FR-2.1, FR-2.2, PCR-2

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          FHIR R4 REST 인터페이스                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  프로토콜: HTTP/1.1 (프로덕션에서는 HTTPS)                                  │
│  Content-Type: application/fhir+json, application/fhir+xml                  │
│  기본 포트: 8080                                                             │
│  Base URL: /fhir                                                             │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ 지원 리소스                                                              ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  리소스           │ 읽기 │ 검색 │ 생성 │ 업데이트 │ 삭제                 ││
│  │  ─────────────────┼──────┼──────┼──────┼──────────┼─────                 ││
│  │  Patient          │  ✓   │  ✓   │  ✗   │    ✗     │  ✗                  ││
│  │  ServiceRequest   │  ✓   │  ✓   │  ✓   │    ✓     │  ✓                  ││
│  │  ImagingStudy     │  ✓   │  ✓   │  ✗   │    ✗     │  ✗                  ││
│  │  DiagnosticReport │  ✓   │  ✓   │  ✗   │    ✗     │  ✗                  ││
│  │  Task             │  ✓   │  ✓   │  ✓   │    ✓     │  ✗                  ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ 엔드포인트                                                               ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  GET    /fhir/Patient/{id}              환자 읽기                       ││
│  │  GET    /fhir/Patient?name=xxx          환자 검색                       ││
│  │                                                                          ││
│  │  GET    /fhir/ServiceRequest/{id}       오더 읽기                       ││
│  │  GET    /fhir/ServiceRequest?patient=xx 오더 검색                       ││
│  │  POST   /fhir/ServiceRequest            오더 생성                       ││
│  │  PUT    /fhir/ServiceRequest/{id}       오더 업데이트                   ││
│  │  DELETE /fhir/ServiceRequest/{id}       오더 취소                       ││
│  │                                                                          ││
│  │  GET    /fhir/ImagingStudy/{id}         검사 읽기                       ││
│  │  GET    /fhir/ImagingStudy?patient=xx   검사 검색                       ││
│  │                                                                          ││
│  │  GET    /fhir/DiagnosticReport/{id}     리포트 읽기                     ││
│  │  GET    /fhir/DiagnosticReport?study=xx 리포트 검색                     ││
│  │                                                                          ││
│  │  GET    /fhir/metadata                  Capability Statement            ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ 검색 매개변수                                                            ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  Patient:                                                                ││
│  │    • _id, identifier, name, birthdate, gender                           ││
│  │                                                                          ││
│  │  ServiceRequest:                                                         ││
│  │    • _id, patient, status, intent, authored, requester                  ││
│  │                                                                          ││
│  │  ImagingStudy:                                                           ││
│  │    • _id, patient, status, started, modality, identifier               ││
│  │                                                                          ││
│  │  DiagnosticReport:                                                       ││
│  │    • _id, patient, status, issued, result                               ││
│  │                                                                          ││
│  │  공통:                                                                   ││
│  │    • _count (페이지네이션), _sort, _include                             ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ 응답 코드                                                                ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  200 OK                 읽기/검색 성공                                  ││
│  │  201 Created            리소스 생성됨                                   ││
│  │  204 No Content         삭제 성공                                       ││
│  │  400 Bad Request        잘못된 요청/리소스                              ││
│  │  401 Unauthorized       인증 필요                                       ││
│  │  404 Not Found          리소스 없음                                     ││
│  │  422 Unprocessable      검증 오류                                       ││
│  │  500 Internal Error     서버 오류                                       ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### INT-EXT-003: pacs_system DICOM 인터페이스

**추적 대상:** IR-1

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        pacs_system DICOM 인터페이스                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  프로토콜: DICOM Upper Layer (PS3.8)                                        │
│  전송: TCP/IP (TLS 선택사항)                                                │
│  기본 포트: 11112                                                            │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ 사용 SOP 클래스                                                          ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  브릿지가 SCU (클라이언트)로서:                                         ││
│  │                                                                          ││
│  │    • Modality Worklist Information Model - FIND                         ││
│  │      UID: 1.2.840.10008.5.1.4.31                                        ││
│  │      목적: 워크리스트 항목 조회/업데이트                                ││
│  │                                                                          ││
│  │    • Modality Performed Procedure Step SOP Class                        ││
│  │      UID: 1.2.840.10008.3.1.2.3.3                                       ││
│  │      목적: MPPS 알림 수신                                               ││
│  │                                                                          ││
│  │    • Patient Root Query/Retrieve - FIND                                 ││
│  │      UID: 1.2.840.10008.5.1.4.1.2.1.1                                   ││
│  │      목적: 환자/검사 정보 조회                                          ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ MWL 조회 속성                                                            ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  조회 키:                                                                ││
│  │    (0008,0050) AccessionNumber                                          ││
│  │    (0010,0020) PatientID                                                ││
│  │    (0040,0100) ScheduledProcedureStepSequence                           ││
│  │      └─ (0040,0001) ScheduledStationAETitle                             ││
│  │      └─ (0040,0002) ScheduledProcedureStepStartDate                     ││
│  │      └─ (0008,0060) Modality                                            ││
│  │                                                                          ││
│  │  반환 키:                                                                ││
│  │    (0010,0010) PatientName                                              ││
│  │    (0010,0020) PatientID                                                ││
│  │    (0010,0030) PatientBirthDate                                         ││
│  │    (0010,0040) PatientSex                                               ││
│  │    (0020,000D) StudyInstanceUID                                         ││
│  │    (0008,0050) AccessionNumber                                          ││
│  │    (0040,1001) RequestedProcedureID                                     ││
│  │    (0040,0100) ScheduledProcedureStepSequence                           ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │ MPPS 속성                                                                ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                                                                          ││
│  │  N-CREATE (IN PROGRESS):                                                ││
│  │    (0008,1115) ReferencedSeriesSequence                                 ││
│  │    (0040,0241) PerformedStationAETitle                                  ││
│  │    (0040,0244) PerformedProcedureStepStartDate                          ││
│  │    (0040,0245) PerformedProcedureStepStartTime                          ││
│  │    (0040,0252) PerformedProcedureStepStatus = "IN PROGRESS"             ││
│  │    (0040,0270) ScheduledStepAttributesSequence                          ││
│  │                                                                          ││
│  │  N-SET (COMPLETED/DISCONTINUED):                                        ││
│  │    (0040,0250) PerformedProcedureStepEndDate                            ││
│  │    (0040,0251) PerformedProcedureStepEndTime                            ││
│  │    (0040,0252) PerformedProcedureStepStatus                             ││
│  │    (0040,0340) PerformedSeriesSequence                                  ││
│  │                                                                          ││
│  └─────────────────────────────────────────────────────────────────────────┘│
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. 공개 API 인터페이스

### INT-API-001: bridge_server 인터페이스

**추적 대상:** 메인 서버 오케스트레이션

```cpp
namespace pacs::bridge {

/**
 * @brief 메인 브릿지 서버
 *
 * 모든 게이트웨이 컴포넌트를 오케스트레이션.
 */
class bridge_server {
public:
    // ═══════════════════════════════════════════════════════════════════
    // 생성
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 설정으로 브릿지 서버 구성
     * @param config 브릿지 설정
     * @throws std::invalid_argument 설정이 유효하지 않은 경우
     */
    explicit bridge_server(const config::bridge_config& config);

    /**
     * @brief 설정 파일로부터 구성
     * @param config_path YAML/JSON 설정 경로
     */
    explicit bridge_server(const std::filesystem::path& config_path);

    // ═══════════════════════════════════════════════════════════════════
    // 생명주기
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 모든 서비스 시작
     * @return 성공 또는 상세 오류
     */
    [[nodiscard]] Result<void> start();

    /**
     * @brief 모든 서비스 정상 종료
     * @param timeout 대기 중인 작업 최대 대기 시간
     */
    void stop(std::chrono::seconds timeout = std::chrono::seconds{30});

    /**
     * @brief 종료 신호까지 블록
     */
    void wait_for_shutdown();

    /**
     * @brief 서버 실행 중 여부 확인
     */
    [[nodiscard]] bool is_running() const noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // 런타임 설정
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 설정 리로드 (핫 리로드)
     * @param config_path 새 설정 파일 경로
     */
    [[nodiscard]] Result<void> reload_config(
        const std::filesystem::path& config_path);

    /**
     * @brief 아웃바운드 목적지 동적 추가
     */
    [[nodiscard]] Result<void> add_destination(
        const mllp::mllp_client_config& dest);

    /**
     * @brief 아웃바운드 목적지 제거
     */
    void remove_destination(std::string_view name);

    // ═══════════════════════════════════════════════════════════════════
    // 모니터링
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 서버 통계 조회
     */
    struct statistics {
        // MLLP 통계
        size_t mllp_active_connections;   // 활성 연결
        size_t mllp_messages_received;    // 수신 메시지
        size_t mllp_messages_sent;        // 송신 메시지
        size_t mllp_errors;               // 오류

        // FHIR 통계
        size_t fhir_requests;             // FHIR 요청
        size_t fhir_errors;               // FHIR 오류

        // 큐 통계
        size_t queue_depth;               // 큐 깊이
        size_t queue_dead_letters;        // Dead Letter

        // 캐시 통계
        size_t cache_size;                // 캐시 크기
        double cache_hit_rate;            // 캐시 히트율

        // 가동 시간
        std::chrono::seconds uptime;      // 업타임
    };

    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief 상태 확인
     * @return 모든 컴포넌트가 정상이면 true
     */
    [[nodiscard]] bool is_healthy() const;

    /**
     * @brief 컴포넌트 상태 상세 조회
     */
    struct health_status {
        bool mllp_server_healthy;        // MLLP 서버 상태
        bool fhir_server_healthy;        // FHIR 서버 상태
        bool pacs_connection_healthy;    // PACS 연결 상태
        bool queue_healthy;              // 큐 상태
        std::string details;             // 상세 정보
    };

    [[nodiscard]] health_status get_health_status() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge
```

### INT-API-002: hl7_gateway 인터페이스

**추적 대상:** FR-1

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief HL7 게이트웨이 공개 인터페이스
 *
 * HL7 v2.x 메시지 처리 기능 제공.
 */
class hl7_gateway {
public:
    explicit hl7_gateway(const hl7_gateway_config& config);

    // ═══════════════════════════════════════════════════════════════════
    // 핸들러 등록
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 특정 타입에 대한 메시지 핸들러 설정
     * @param message_type 메시지 타입 패턴 (예: "ADT^A*", "ORM^O01")
     * @param handler 핸들러 함수
     *
     * 핸들러 시그니처:
     *   Result<std::string>(const hl7_message& message)
     *   ACK 메시지 또는 오류 반환
     */
    void set_handler(
        std::string_view message_type,
        std::function<Result<std::string>(const hl7_message&)> handler);

    /**
     * @brief 매칭되지 않은 메시지에 대한 기본 핸들러 설정
     */
    void set_default_handler(
        std::function<Result<std::string>(const hl7_message&)> handler);

    /**
     * @brief 전처리 훅 설정
     * @param hook 메시지 처리 전 호출
     *             false 반환 시 메시지 거부
     */
    void set_pre_hook(
        std::function<bool(const std::string& raw)> hook);

    /**
     * @brief 후처리 훅 설정
     * @param hook 처리 성공 후 호출
     */
    void set_post_hook(
        std::function<void(const hl7_message&, const std::string& ack)> hook);

    // ═══════════════════════════════════════════════════════════════════
    // 생명주기
    // ═══════════════════════════════════════════════════════════════════

    [[nodiscard]] Result<void> start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // 아웃바운드 메시징
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 목적지로 메시지 송신
     * @param destination 목적지 이름 (설정에서)
     * @param message 송신할 HL7 메시지
     * @return ACK 응답 또는 오류
     */
    [[nodiscard]] Result<std::string> send(
        std::string_view destination,
        const std::string& message);

    /**
     * @brief 비동기 메시지 송신
     */
    [[nodiscard]] std::future<Result<std::string>> send_async(
        std::string_view destination,
        const std::string& message);

    /**
     * @brief 신뢰성 있는 전달을 위한 메시지 큐잉
     */
    [[nodiscard]] Result<std::string> queue_send(
        std::string_view destination,
        const std::string& message,
        int priority = 0);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::hl7
```

---

## 4. 내부 모듈 인터페이스

### INT-MOD-001: hl7_message 접근

**추적 대상:** DES-HL7-001

```cpp
namespace pacs::bridge::hl7 {

// 편의를 위한 공통 HL7 필드 경로
namespace field_paths {
    // MSH 세그먼트
    constexpr auto MSH_SENDING_APP = "MSH-3";      // 송신 애플리케이션
    constexpr auto MSH_SENDING_FAC = "MSH-4";      // 송신 기관
    constexpr auto MSH_RECEIVING_APP = "MSH-5";    // 수신 애플리케이션
    constexpr auto MSH_RECEIVING_FAC = "MSH-6";    // 수신 기관
    constexpr auto MSH_DATETIME = "MSH-7";         // 날짜/시간
    constexpr auto MSH_MESSAGE_TYPE = "MSH-9";     // 메시지 타입
    constexpr auto MSH_CONTROL_ID = "MSH-10";      // 제어 ID
    constexpr auto MSH_VERSION = "MSH-12";         // 버전

    // PID 세그먼트
    constexpr auto PID_SET_ID = "PID-1";           // Set ID
    constexpr auto PID_PATIENT_ID = "PID-3";       // 환자 ID
    constexpr auto PID_PATIENT_NAME = "PID-5";     // 환자 이름
    constexpr auto PID_BIRTH_DATE = "PID-7";       // 생년월일
    constexpr auto PID_SEX = "PID-8";              // 성별

    // ORC 세그먼트
    constexpr auto ORC_ORDER_CONTROL = "ORC-1";    // 오더 제어
    constexpr auto ORC_PLACER_ORDER = "ORC-2";     // Placer 오더
    constexpr auto ORC_FILLER_ORDER = "ORC-3";     // Filler 오더
    constexpr auto ORC_ORDER_STATUS = "ORC-5";     // 오더 상태

    // OBR 세그먼트
    constexpr auto OBR_SET_ID = "OBR-1";           // Set ID
    constexpr auto OBR_PLACER_ORDER = "OBR-2";     // Placer 오더
    constexpr auto OBR_FILLER_ORDER = "OBR-3";     // Filler 오더
    constexpr auto OBR_PROCEDURE_CODE = "OBR-4";   // 검사 코드
    constexpr auto OBR_SCHEDULED_DT = "OBR-7";     // 예약 일시
    constexpr auto OBR_MODALITY = "OBR-24";        // 모달리티

    // ZDS 세그먼트 (Study Instance UID)
    constexpr auto ZDS_STUDY_UID = "ZDS-1";        // Study UID
}

/**
 * @brief HL7 메시지 유틸리티
 */
class hl7_utils {
public:
    /**
     * @brief 메시지에서 환자 정보 추출
     */
    [[nodiscard]] static pacs_adapter::patient_info
        extract_patient(const hl7_message& msg);

    /**
     * @brief ORM에서 오더 정보 추출
     */
    struct order_info {
        std::string order_control;         // 오더 제어
        std::string placer_order_number;   // Placer 오더 번호
        std::string filler_order_number;   // Filler 오더 번호
        std::string accession_number;      // Accession 번호
        std::string procedure_code;        // 검사 코드
        std::string modality;              // 모달리티
        std::string scheduled_datetime;    // 예약 일시
        std::string study_uid;             // Study UID
    };

    [[nodiscard]] static order_info extract_order(const hl7_message& msg);

    /**
     * @brief 메시지가 신규 오더인지 확인
     */
    [[nodiscard]] static bool is_new_order(const hl7_message& msg);

    /**
     * @brief 메시지가 오더 취소인지 확인
     */
    [[nodiscard]] static bool is_cancel_order(const hl7_message& msg);

    /**
     * @brief 메시지가 오더 수정인지 확인
     */
    [[nodiscard]] static bool is_modify_order(const hl7_message& msg);
};

} // namespace pacs::bridge::hl7
```

### INT-MOD-002: 매퍼 인터페이스

**추적 대상:** DES-TRANS-001, DES-TRANS-002

```cpp
namespace pacs::bridge::mapping {

/**
 * @brief 기본 매퍼 인터페이스
 */
class mapper_interface {
public:
    virtual ~mapper_interface() = default;

    /**
     * @brief 매퍼 이름 조회
     */
    [[nodiscard]] virtual std::string_view name() const = 0;

    /**
     * @brief 이 메시지 타입을 처리하는지 확인
     */
    [[nodiscard]] virtual bool handles(
        std::string_view message_type) const = 0;
};

/**
 * @brief HL7에서 DICOM으로 매퍼 인터페이스
 */
class hl7_to_dicom_mapper : public mapper_interface {
public:
    /**
     * @brief HL7 메시지를 DICOM 데이터셋으로 매핑
     */
    [[nodiscard]] virtual Result<pacs::core::dicom_dataset> map(
        const hl7::hl7_message& message) = 0;
};

/**
 * @brief DICOM에서 HL7로 매퍼 인터페이스
 */
class dicom_to_hl7_mapper : public mapper_interface {
public:
    /**
     * @brief DICOM 데이터셋을 HL7 메시지로 매핑
     */
    [[nodiscard]] virtual Result<std::string> map(
        const pacs::core::dicom_dataset& dataset,
        std::string_view event_type) = 0;
};

/**
 * @brief 매퍼 레지스트리
 */
class mapper_registry {
public:
    /**
     * @brief HL7→DICOM 매퍼 등록
     */
    void register_mapper(std::shared_ptr<hl7_to_dicom_mapper> mapper);

    /**
     * @brief DICOM→HL7 매퍼 등록
     */
    void register_mapper(std::shared_ptr<dicom_to_hl7_mapper> mapper);

    /**
     * @brief HL7 메시지 타입에 대한 매퍼 찾기
     */
    [[nodiscard]] std::shared_ptr<hl7_to_dicom_mapper>
        find_hl7_mapper(std::string_view message_type) const;

    /**
     * @brief DICOM 이벤트에 대한 매퍼 찾기
     */
    [[nodiscard]] std::shared_ptr<dicom_to_hl7_mapper>
        find_dicom_mapper(std::string_view event_type) const;

private:
    std::vector<std::shared_ptr<hl7_to_dicom_mapper>> hl7_mappers_;
    std::vector<std::shared_ptr<dicom_to_hl7_mapper>> dicom_mappers_;
};

} // namespace pacs::bridge::mapping
```

---

## 5. 에코시스템 통합 인터페이스

### INT-ECO-001: Result<T> 패턴

**추적 대상:** common_system 통합

```cpp
namespace pacs::bridge {

// 오류 처리를 위해 common_system Result<T> 사용
template<typename T>
using Result = common::Result<T>;

/**
 * @brief 오류 생성 헬퍼
 */
namespace errors {

inline Result<void> invalid_message(std::string_view details) {
    return Result<void>::err(-900, fmt::format("유효하지 않은 메시지: {}", details));
}

inline Result<void> missing_segment(std::string_view segment) {
    return Result<void>::err(-902, fmt::format("세그먼트 누락: {}", segment));
}

inline Result<void> connection_failed(std::string_view host, uint16_t port) {
    return Result<void>::err(-920,
        fmt::format("연결 실패: {}:{}", host, port));
}

inline Result<void> mapping_failed(std::string_view reason) {
    return Result<void>::err(-940, fmt::format("매핑 실패: {}", reason));
}

} // namespace errors

} // namespace pacs::bridge
```

### INT-ECO-002: 로거 통합

**추적 대상:** logger_system 통합

```cpp
namespace pacs::bridge::integration {

/**
 * @brief 로그 레벨
 */
enum class log_level {
    trace,    // 추적
    debug,    // 디버그
    info,     // 정보
    warn,     // 경고
    error     // 오류
};

/**
 * @brief 브릿지 전용 로그 카테고리
 */
namespace log_category {
    constexpr auto HL7 = "hl7";           // HL7 관련
    constexpr auto MLLP = "mllp";         // MLLP 관련
    constexpr auto FHIR = "fhir";         // FHIR 관련
    constexpr auto MAPPING = "mapping";   // 매핑 관련
    constexpr auto ROUTING = "routing";   // 라우팅 관련
    constexpr auto QUEUE = "queue";       // 큐 관련
    constexpr auto PACS = "pacs";         // PACS 관련
    constexpr auto AUDIT = "audit";       // 감사 로그
}

/**
 * @brief 구조화된 로그 엔트리
 */
struct log_entry {
    log_level level;                               // 레벨
    std::string category;                          // 카테고리
    std::string message;                           // 메시지
    std::map<std::string, std::string> fields;     // 필드
    std::chrono::system_clock::time_point timestamp; // 타임스탬프
};

/**
 * @brief 편의를 위한 로그 매크로
 */
#define BRIDGE_LOG_TRACE(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::trace, category, msg, ##__VA_ARGS__)

#define BRIDGE_LOG_DEBUG(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::debug, category, msg, ##__VA_ARGS__)

#define BRIDGE_LOG_INFO(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::info, category, msg, ##__VA_ARGS__)

#define BRIDGE_LOG_WARN(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::warn, category, msg, ##__VA_ARGS__)

#define BRIDGE_LOG_ERROR(category, msg, ...) \
    pacs::bridge::integration::logger_adapter::log( \
        log_level::error, category, msg, ##__VA_ARGS__)

} // namespace pacs::bridge::integration
```

---

## 6. 오류 처리 인터페이스

### INT-ERR-001: 오류 코드

**추적 대상:** PRD 부록 C

```cpp
namespace pacs::bridge::error_codes {

// ═══════════════════════════════════════════════════════════════════════════
// HL7 파싱 오류 (-900 ~ -919)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int INVALID_HL7_MESSAGE = -900;        // 유효하지 않은 HL7 메시지
constexpr int MISSING_MSH_SEGMENT = -901;        // MSH 세그먼트 누락
constexpr int INVALID_SEGMENT_STRUCTURE = -902;  // 유효하지 않은 세그먼트 구조
constexpr int MISSING_REQUIRED_FIELD = -903;     // 필수 필드 누락
constexpr int INVALID_FIELD_VALUE = -904;        // 유효하지 않은 필드 값
constexpr int UNKNOWN_MESSAGE_TYPE = -905;       // 알 수 없는 메시지 타입
constexpr int UNSUPPORTED_VERSION = -906;        // 지원하지 않는 버전
constexpr int INVALID_DELIMITER = -907;          // 유효하지 않은 구분자
constexpr int ENCODING_ERROR = -908;             // 인코딩 오류

// ═══════════════════════════════════════════════════════════════════════════
// MLLP 전송 오류 (-920 ~ -939)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int MLLP_CONNECTION_FAILED = -920;     // MLLP 연결 실패
constexpr int MLLP_SEND_FAILED = -921;           // MLLP 송신 실패
constexpr int MLLP_RECEIVE_TIMEOUT = -922;       // MLLP 수신 타임아웃
constexpr int MLLP_INVALID_FRAME = -923;         // 유효하지 않은 MLLP 프레임
constexpr int MLLP_TLS_HANDSHAKE_FAILED = -924;  // TLS 핸드셰이크 실패
constexpr int MLLP_CONNECTION_RESET = -925;      // 연결 리셋
constexpr int MLLP_SERVER_UNAVAILABLE = -926;    // 서버 불가용

// ═══════════════════════════════════════════════════════════════════════════
// 변환/매핑 오류 (-940 ~ -959)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int MAPPING_FAILED = -940;             // 매핑 실패
constexpr int MISSING_MAPPING_CONFIG = -941;     // 매핑 설정 누락
constexpr int INVALID_CODE_SYSTEM = -942;        // 유효하지 않은 코드 시스템
constexpr int PATIENT_NOT_FOUND = -943;          // 환자 없음
constexpr int ORDER_NOT_FOUND = -944;            // 오더 없음
constexpr int INVALID_DATE_FORMAT = -945;        // 유효하지 않은 날짜 형식
constexpr int INVALID_NAME_FORMAT = -946;        // 유효하지 않은 이름 형식
constexpr int UID_GENERATION_FAILED = -947;      // UID 생성 실패

// ═══════════════════════════════════════════════════════════════════════════
// 큐 오류 (-960 ~ -979)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int QUEUE_FULL = -960;                 // 큐 가득 참
constexpr int MESSAGE_EXPIRED = -961;            // 메시지 만료
constexpr int DELIVERY_FAILED = -962;            // 전달 실패
constexpr int RETRY_LIMIT_EXCEEDED = -963;       // 재시도 한도 초과
constexpr int QUEUE_DATABASE_ERROR = -964;       // 큐 데이터베이스 오류
constexpr int MESSAGE_NOT_FOUND = -965;          // 메시지 없음

// ═══════════════════════════════════════════════════════════════════════════
// pacs_system 통합 오류 (-980 ~ -999)
// ═══════════════════════════════════════════════════════════════════════════

constexpr int PACS_CONNECTION_FAILED = -980;     // PACS 연결 실패
constexpr int MWL_UPDATE_FAILED = -981;          // MWL 업데이트 실패
constexpr int MPPS_HANDLER_ERROR = -982;         // MPPS 핸들러 오류
constexpr int DICOM_TRANSLATION_ERROR = -983;    // DICOM 변환 오류
constexpr int PACS_ASSOCIATION_FAILED = -984;    // PACS Association 실패
constexpr int MWL_QUERY_FAILED = -985;           // MWL 조회 실패
constexpr int PATIENT_CACHE_ERROR = -986;        // 환자 캐시 오류

/**
 * @brief 사람이 읽을 수 있는 오류 메시지 조회
 */
[[nodiscard]] std::string_view message(int code);

/**
 * @brief 오류 카테고리 조회
 */
[[nodiscard]] std::string_view category(int code);

/**
 * @brief 오류가 복구 가능한지 확인
 */
[[nodiscard]] bool is_recoverable(int code);

/**
 * @brief 오류에 대한 권장 조치 조회
 */
[[nodiscard]] std::string_view suggested_action(int code);

} // namespace pacs::bridge::error_codes
```

---

## 7. 설정 인터페이스

### INT-CFG-001: 설정 파일 형식

**추적 대상:** FR-5, PRD 부록 D

```yaml
# pacs_bridge.yaml - 설정 예시
# ═══════════════════════════════════════════════════════════════════════════

# 서버 식별
name: "PACS_BRIDGE"

# ═══════════════════════════════════════════════════════════════════════════
# HL7 게이트웨이 설정
# ═══════════════════════════════════════════════════════════════════════════
hl7:
  listener:
    port: 2575
    max_connections: 50
    connection_timeout_seconds: 60
    receive_timeout_seconds: 30
    tls_enabled: false
    # cert_path: "/etc/pacs_bridge/cert.pem"
    # key_path: "/etc/pacs_bridge/key.pem"

  outbound:
    - name: "RIS_PRIMARY"
      host: "ris.hospital.local"
      port: 2576
      tls_enabled: false
      keep_alive: true
      retry_count: 3
      retry_delay_ms: 1000

    - name: "RIS_BACKUP"
      host: "ris-backup.hospital.local"
      port: 2576
      tls_enabled: false

# ═══════════════════════════════════════════════════════════════════════════
# FHIR 게이트웨이 설정 (Phase 3)
# ═══════════════════════════════════════════════════════════════════════════
fhir:
  enabled: false
  port: 8080
  base_path: "/fhir"
  tls_enabled: false
  max_connections: 100
  page_size: 100

# ═══════════════════════════════════════════════════════════════════════════
# pacs_system 연결
# ═══════════════════════════════════════════════════════════════════════════
pacs:
  host: "localhost"
  port: 11112
  ae_title: "PACS_BRIDGE"
  called_ae: "PACS_SCP"
  timeout_seconds: 30

# ═══════════════════════════════════════════════════════════════════════════
# 매핑 설정
# ═══════════════════════════════════════════════════════════════════════════
mapping:
  institution_name: "종합병원"
  uid_root: "1.2.840.113619.2.55"
  patient_id_issuer: "HOSPITAL_MRN"

  procedure_to_modality:
    "CT_CHEST": "CT"
    "CT_ABDOMEN": "CT"
    "MR_BRAIN": "MR"
    "MR_SPINE": "MR"
    "US_ABDOMEN": "US"
    "XR_CHEST": "CR"

  station_ae_mapping:
    CT: ["CT_SCANNER_1", "CT_SCANNER_2"]
    MR: ["MR_SCANNER_1"]
    US: ["US_ROOM_1", "US_ROOM_2"]
    CR: ["CR_ROOM_1"]

# ═══════════════════════════════════════════════════════════════════════════
# 라우팅 설정
# ═══════════════════════════════════════════════════════════════════════════
routing:
  rules:
    - name: "ADT_to_cache"
      message_type_pattern: "ADT^A*"
      destination: "patient_cache"
      priority: 10

    - name: "ORM_to_mwl"
      message_type_pattern: "ORM^O01"
      destination: "mwl_handler"
      priority: 10

    - name: "default_log"
      message_type_pattern: "*"
      destination: "log_handler"
      priority: 0

# ═══════════════════════════════════════════════════════════════════════════
# 큐 설정
# ═══════════════════════════════════════════════════════════════════════════
queue:
  database_path: "/var/lib/pacs_bridge/queue.db"
  max_queue_size: 50000
  max_retry_count: 5
  initial_retry_delay_seconds: 5
  retry_backoff_multiplier: 2.0
  max_retry_delay_seconds: 300
  message_ttl_hours: 24

# ═══════════════════════════════════════════════════════════════════════════
# 환자 캐시 설정
# ═══════════════════════════════════════════════════════════════════════════
patient_cache:
  max_entries: 10000
  ttl_seconds: 3600
  evict_on_full: true

# ═══════════════════════════════════════════════════════════════════════════
# 로깅 설정
# ═══════════════════════════════════════════════════════════════════════════
logging:
  level: "INFO"  # TRACE, DEBUG, INFO, WARN, ERROR
  format: "json"  # json, text
  file: "/var/log/pacs_bridge/bridge.log"
  audit_file: "/var/log/pacs_bridge/audit.log"
  max_file_size_mb: 100
  max_files: 10
```

---

*문서 버전: 1.0.0*
*작성일: 2025-12-07*
*작성자: kcenon@naver.com*
