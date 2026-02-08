# SDS - 컴포넌트 설계

> **버전:** 0.2.0.0
> **상위 문서:** [SDS_KO.md](SDS_KO.md)
> **최종 수정일:** 2026-02-08

---

## 목차

- [1. HL7 게이트웨이 모듈](#1-hl7-게이트웨이-모듈)
- [2. MLLP 전송 모듈](#2-mllp-전송-모듈)
- [3. FHIR 게이트웨이 모듈](#3-fhir-게이트웨이-모듈)
- [4. 변환 계층 모듈](#4-변환-계층-모듈)
- [5. 메시지 라우팅 모듈](#5-메시지-라우팅-모듈)
- [6. pacs_system 어댑터 모듈](#6-pacs_system-어댑터-모듈)
- [7. 설정 모듈](#7-설정-모듈)
- [8. 통합 모듈](#8-통합-모듈)

---

## 1. HL7 게이트웨이 모듈

### 1.1 모듈 개요

**네임스페이스:** `pacs::bridge::hl7`
**목적:** HL7 v2.x 메시지 파싱, 검증, 생성

```
┌────────────────────────────────────────────────────────────────────────────┐
│                           HL7 게이트웨이 모듈                               │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌─────────────────────────────────────────────────────────────────────┐  │
│   │                         hl7_message                                  │  │
│   │                                                                      │  │
│   │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────────┐   │  │
│   │  │   segment    │  │    field     │  │      component           │   │  │
│   │  │  (MSH, PID)  │  │   (PID-3)    │  │    (PID-5.1)             │   │  │
│   │  └──────────────┘  └──────────────┘  └──────────────────────────┘   │  │
│   └─────────────────────────────────────────────────────────────────────┘  │
│                                      ▲                                      │
│                                      │                                      │
│         ┌────────────────────────────┼────────────────────────────┐        │
│         │                            │                            │        │
│   ┌─────┴─────────┐           ┌──────┴──────┐           ┌─────────┴─────┐  │
│   │  hl7_parser   │           │hl7_validator│           │  hl7_builder  │  │
│   │               │           │             │           │               │  │
│   │ parse(raw)    │           │ validate()  │           │ build()       │  │
│   │ → hl7_message │           │ → Result<>  │           │ → string      │  │
│   └───────────────┘           └─────────────┘           └───────────────┘  │
│                                                                             │
└────────────────────────────────────────────────────────────────────────────┘
```

### DES-HL7-001: hl7_message

**추적 대상:** FR-1.1.1, FR-1.1.2, FR-1.1.3

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief HL7 v2.x 메시지 표현
 *
 * 메시지 내용에 대한 계층적 접근 제공:
 *   메시지 → 세그먼트 → 필드 → 컴포넌트 → 서브컴포넌트
 */
class hl7_message {
public:
    // ═══════════════════════════════════════════════════════════════════
    // 메시지 헤더 접근
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 메시지 타입 조회 (예: "ADT^A01")
     */
    [[nodiscard]] std::string message_type() const;

    /**
     * @brief 메시지 제어 ID 조회 (MSH-10)
     */
    [[nodiscard]] std::string control_id() const;

    /**
     * @brief HL7 버전 조회 (예: "2.5.1")
     */
    [[nodiscard]] std::string version() const;

    /**
     * @brief 송신 애플리케이션 조회 (MSH-3)
     */
    [[nodiscard]] std::string sending_application() const;

    /**
     * @brief 송신 기관 조회 (MSH-4)
     */
    [[nodiscard]] std::string sending_facility() const;

    // ═══════════════════════════════════════════════════════════════════
    // 세그먼트 접근
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 이름으로 세그먼트 조회
     * @param name 세그먼트 식별자 (예: "PID", "OBR")
     * @return 세그먼트 포인터 또는 찾지 못한 경우 nullptr
     */
    [[nodiscard]] const hl7_segment* segment(std::string_view name) const;

    /**
     * @brief 지정된 이름의 모든 세그먼트 조회
     * @param name 세그먼트 식별자
     * @return 일치하는 세그먼트 벡터 (반복 세그먼트용)
     */
    [[nodiscard]] std::vector<const hl7_segment*>
        segments(std::string_view name) const;

    /**
     * @brief 세그먼트 존재 여부 확인
     */
    [[nodiscard]] bool has_segment(std::string_view name) const;

    // ═══════════════════════════════════════════════════════════════════
    // 필드 접근 (편의 기능)
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 경로로 필드 값 조회
     * @param path 필드 경로 (예: "PID-3", "OBR-4.1")
     * @return 필드 값 또는 찾지 못한 경우 빈 문자열
     *
     * 경로 형식: 세그먼트-필드[.컴포넌트[.서브컴포넌트]]
     */
    [[nodiscard]] std::string get_field(std::string_view path) const;

    /**
     * @brief 반복 필드 값 조회
     * @param path 필드 경로 (예: "PID-3")
     * @return 반복 필드의 값 벡터
     */
    [[nodiscard]] std::vector<std::string>
        get_repeating_field(std::string_view path) const;

    // ═══════════════════════════════════════════════════════════════════
    // 반복자
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 모든 세그먼트 반복
     */
    [[nodiscard]] auto begin() const;
    [[nodiscard]] auto end() const;

    /**
     * @brief 세그먼트 개수 조회
     */
    [[nodiscard]] size_t segment_count() const noexcept;

private:
    std::vector<hl7_segment> segments_;
    hl7_delimiters delimiters_;
};

/**
 * @brief HL7 세그먼트 표현
 */
class hl7_segment {
public:
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] const hl7_field& field(size_t index) const;
    [[nodiscard]] size_t field_count() const noexcept;

    // 편의 접근
    [[nodiscard]] std::string get_value(size_t field_index,
                                         size_t component = 0,
                                         size_t subcomponent = 0) const;
};

/**
 * @brief HL7 필드 표현 (반복 지원)
 */
class hl7_field {
public:
    [[nodiscard]] bool is_repeating() const noexcept;
    [[nodiscard]] size_t repetition_count() const noexcept;

    [[nodiscard]] std::string_view value() const;
    [[nodiscard]] std::string_view value(size_t repetition) const;

    [[nodiscard]] const hl7_component& component(size_t index,
                                                  size_t repetition = 0) const;
};

/**
 * @brief HL7 구분자 설정
 */
struct hl7_delimiters {
    char field_separator = '|';      // MSH-1
    char component_separator = '^';  // MSH-2[0]
    char repetition_separator = '~'; // MSH-2[1]
    char escape_character = '\\';    // MSH-2[2]
    char subcomponent_separator = '&'; // MSH-2[3]
};

} // namespace pacs::bridge::hl7
```

### DES-HL7-002: hl7_parser

**추적 대상:** FR-1.1.1, FR-1.1.2, FR-1.1.3, FR-1.1.5

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief HL7 v2.x 메시지 파서
 *
 * 세그먼트별 검증을 통한 스트리밍 파서.
 * 스레드 안전: 상태 없는 연산.
 */
class hl7_parser {
public:
    /**
     * @brief 원시 HL7 메시지 파싱
     * @param raw 원시 메시지 바이트 (MLLP 프레이밍 제거됨)
     * @return 파싱된 메시지 또는 오류
     *
     * 오류:
     *   -900: INVALID_HL7_MESSAGE (유효하지 않은 HL7 메시지)
     *   -901: MISSING_MSH_SEGMENT (MSH 세그먼트 누락)
     *   -902: INVALID_SEGMENT_STRUCTURE (유효하지 않은 세그먼트 구조)
     *   -903: MISSING_REQUIRED_FIELD (필수 필드 누락)
     */
    [[nodiscard]] static Result<hl7_message> parse(std::string_view raw);

    /**
     * @brief 특정 버전 검증을 통한 파싱
     * @param raw 원시 메시지
     * @param expected_version 예상 HL7 버전 (예: "2.5.1")
     */
    [[nodiscard]] static Result<hl7_message> parse(
        std::string_view raw,
        std::string_view expected_version);

    /**
     * @brief MSH 세그먼트만 파싱 (라우팅 결정용)
     * @param raw 원시 메시지
     * @return MSH 세그먼트 또는 오류
     */
    [[nodiscard]] static Result<hl7_segment> parse_msh(std::string_view raw);

private:
    static Result<hl7_delimiters> parse_delimiters(std::string_view msh);
    static Result<hl7_segment> parse_segment(
        std::string_view raw,
        const hl7_delimiters& delimiters);
    static std::string unescape(
        std::string_view value,
        const hl7_delimiters& delimiters);
};

} // namespace pacs::bridge::hl7
```

### DES-HL7-003: hl7_builder

**추적 대상:** FR-1.2.3, FR-1.2.5

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief HL7 메시지 빌더 (플루언트 인터페이스)
 *
 * 적절한 포맷팅과 이스케이프를 통한 HL7 메시지 생성.
 */
class hl7_builder {
public:
    /**
     * @brief ACK 메시지 빌드 시작
     * @param original 응답할 원본 메시지
     * @param ack_code 확인 코드 (AA, AE, AR)
     * @return 체이닝을 위한 빌더
     */
    static hl7_builder ack(const hl7_message& original,
                           std::string_view ack_code);

    /**
     * @brief ORU^R01 메시지 빌드 시작
     * @return 체이닝을 위한 빌더
     */
    static hl7_builder oru_r01();

    /**
     * @brief ORM^O01 메시지 빌드 시작
     * @return 체이닝을 위한 빌더
     */
    static hl7_builder orm_o01();

    // ═══════════════════════════════════════════════════════════════════
    // 세그먼트 빌딩
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief MSH 세그먼트 추가
     */
    hl7_builder& msh(std::string_view sending_app,
                     std::string_view sending_facility,
                     std::string_view receiving_app,
                     std::string_view receiving_facility,
                     std::string_view message_type,
                     std::string_view version = "2.5.1");

    /**
     * @brief PID 세그먼트 추가
     */
    hl7_builder& pid(std::string_view patient_id,
                     std::string_view patient_name,
                     std::string_view birth_date = "",
                     std::string_view sex = "");

    /**
     * @brief ORC 세그먼트 추가
     */
    hl7_builder& orc(std::string_view order_control,
                     std::string_view placer_order,
                     std::string_view filler_order = "",
                     std::string_view order_status = "");

    /**
     * @brief OBR 세그먼트 추가
     */
    hl7_builder& obr(int set_id,
                     std::string_view placer_order,
                     std::string_view filler_order,
                     std::string_view procedure_code);

    /**
     * @brief OBX 세그먼트 추가
     */
    hl7_builder& obx(int set_id,
                     std::string_view value_type,
                     std::string_view observation_id,
                     std::string_view value,
                     std::string_view units = "");

    /**
     * @brief MSA 세그먼트 추가 (ACK용)
     */
    hl7_builder& msa(std::string_view ack_code,
                     std::string_view control_id,
                     std::string_view text_message = "");

    /**
     * @brief 사용자 정의 세그먼트 추가
     */
    hl7_builder& segment(std::string_view name,
                         const std::vector<std::string>& fields);

    // ═══════════════════════════════════════════════════════════════════
    // 출력
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 최종 메시지 문자열 빌드
     * @return HL7 메시지 (MLLP 프레이밍 없음)
     */
    [[nodiscard]] std::string build() const;

    /**
     * @brief hl7_message 객체로 빌드
     */
    [[nodiscard]] Result<hl7_message> build_message() const;

private:
    std::vector<std::string> segments_;
    hl7_delimiters delimiters_;
    std::string escape(std::string_view value) const;
};

} // namespace pacs::bridge::hl7
```

### DES-HL7-004: hl7_validator

**추적 대상:** FR-1.1.4, FR-1.2

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief 메시지 타입별 검증
 */
class hl7_validator {
public:
    /**
     * @brief 메시지 구조 검증
     * @param message 파싱된 HL7 메시지
     * @return 상세 정보가 포함된 검증 결과
     */
    [[nodiscard]] static Result<void> validate(const hl7_message& message);

    /**
     * @brief ADT 메시지 검증
     */
    [[nodiscard]] static Result<void> validate_adt(const hl7_message& message);

    /**
     * @brief ORM 메시지 검증
     */
    [[nodiscard]] static Result<void> validate_orm(const hl7_message& message);

    /**
     * @brief ORU 메시지 검증
     */
    [[nodiscard]] static Result<void> validate_oru(const hl7_message& message);

    /**
     * @brief SIU 메시지 검증
     */
    [[nodiscard]] static Result<void> validate_siu(const hl7_message& message);

private:
    static Result<void> check_required_segments(
        const hl7_message& message,
        const std::vector<std::string>& required);

    static Result<void> check_required_fields(
        const hl7_segment& segment,
        const std::vector<int>& required_fields);
};

/**
 * @brief 메시지 타입별 검증 규칙
 */
struct validation_rules {
    std::vector<std::string> required_segments;
    std::map<std::string, std::vector<int>> required_fields;
};

} // namespace pacs::bridge::hl7
```

### DES-HL7-005: adt_handler

**추적 대상:** FR-1.1.2, FR-1.1.3, FR-2.1

```cpp
namespace pacs::bridge::hl7 {

/**
 * @brief ADT 핸들러 전용 오류 코드
 *
 * 할당 범위: -850 ~ -859
 */
enum class adt_error : int {
    not_adt_message = -850,         // ADT 메시지가 아님
    unsupported_trigger_event = -851, // 지원하지 않는 트리거 이벤트
    missing_patient_id = -852,       // 환자 ID 누락
    patient_not_found = -853,        // 환자를 찾을 수 없음
    merge_failed = -854,             // 병합 실패
    cache_operation_failed = -855,   // 캐시 작업 실패
    invalid_patient_data = -856,     // 잘못된 환자 데이터
    duplicate_patient = -857,        // 중복 환자
    handler_not_registered = -858,   // 핸들러 미등록
    processing_failed = -859         // 처리 실패
};

/**
 * @brief 지원되는 ADT 트리거 이벤트
 */
enum class adt_trigger_event {
    A01,  // 입원/방문 통지
    A04,  // 환자 등록
    A08,  // 환자 정보 업데이트
    A40,  // 환자 병합 - 환자 식별자 목록
    unknown
};

/**
 * @brief ADT 메시지 처리 결과
 */
struct adt_result {
    bool success = false;
    adt_trigger_event trigger = adt_trigger_event::unknown;
    std::string patient_id;
    std::string merged_patient_id;
    std::string description;
    hl7_message ack_message;
    std::vector<std::string> warnings;
};

/**
 * @brief ADT 핸들러 설정
 */
struct adt_handler_config {
    bool allow_a01_update = true;     // A01에서 기존 환자 업데이트 허용
    bool allow_a08_create = false;    // A08에서 환자 생성 허용
    bool validate_patient_data = true; // 캐싱 전 환자 데이터 검증
    std::vector<std::string> required_fields = {"patient_id", "patient_name"};
    bool detailed_ack = true;          // 상세 ACK 메시지 생성
    bool audit_logging = true;         // 감사 로깅
    std::string ack_sending_application = "PACS_BRIDGE";
    std::string ack_sending_facility = "RADIOLOGY";
};

/**
 * @brief 환자 병합 작업 정보
 */
struct merge_info {
    std::string primary_patient_id;    // 주(생존) 환자 ID
    std::string secondary_patient_id;  // 부(병합) 환자 ID
    std::string primary_issuer;
    std::string secondary_issuer;
    std::string merge_datetime;
};

/**
 * @brief 환자 인구통계 캐시용 ADT 메시지 핸들러
 *
 * ADT(Admission, Discharge, Transfer) 메시지를 처리하여
 * 캐시에 환자 인구통계 정보를 유지합니다. A01, A04, A08, A40 이벤트를 지원합니다.
 *
 * 내부 뮤텍스로 스레드 안전성을 보장합니다.
 */
class adt_handler {
public:
    using patient_created_callback =
        std::function<void(const mapping::dicom_patient& patient)>;
    using patient_updated_callback =
        std::function<void(const mapping::dicom_patient& old_patient,
                           const mapping::dicom_patient& new_patient)>;
    using patient_merged_callback =
        std::function<void(const merge_info& merge)>;

    /**
     * @brief 환자 캐시로 핸들러 생성
     */
    explicit adt_handler(std::shared_ptr<cache::patient_cache> cache);

    /**
     * @brief 캐시와 설정으로 핸들러 생성
     */
    adt_handler(std::shared_ptr<cache::patient_cache> cache,
                const adt_handler_config& config);

    /**
     * @brief ADT 메시지 처리
     * @param message HL7 ADT 메시지
     * @return 처리 결과 또는 오류
     */
    [[nodiscard]] std::expected<adt_result, adt_error> handle(
        const hl7_message& message);

    /**
     * @brief 메시지 처리 가능 여부 확인
     */
    [[nodiscard]] bool can_handle(const hl7_message& message) const noexcept;

    /**
     * @brief 지원하는 트리거 이벤트 조회
     */
    [[nodiscard]] std::vector<std::string> supported_triggers() const;

    // 개별 이벤트 핸들러
    [[nodiscard]] std::expected<adt_result, adt_error> handle_admit(
        const hl7_message& message);     // A01
    [[nodiscard]] std::expected<adt_result, adt_error> handle_register(
        const hl7_message& message);     // A04
    [[nodiscard]] std::expected<adt_result, adt_error> handle_update(
        const hl7_message& message);     // A08
    [[nodiscard]] std::expected<adt_result, adt_error> handle_merge(
        const hl7_message& message);     // A40

    // 콜백
    void on_patient_created(patient_created_callback callback);
    void on_patient_updated(patient_updated_callback callback);
    void on_patient_merged(patient_merged_callback callback);

    // 설정
    [[nodiscard]] const adt_handler_config& config() const noexcept;
    void set_config(const adt_handler_config& config);
    [[nodiscard]] std::shared_ptr<cache::patient_cache> cache() const noexcept;

    // 통계
    struct statistics {
        size_t total_processed = 0;    // 총 처리 수
        size_t success_count = 0;       // 성공 수
        size_t failure_count = 0;       // 실패 수
        size_t a01_count = 0;           // A01 처리 수
        size_t a04_count = 0;           // A04 처리 수
        size_t a08_count = 0;           // A08 처리 수
        size_t a40_count = 0;           // A40 처리 수
        size_t patients_created = 0;    // 생성된 환자 수
        size_t patients_updated = 0;    // 업데이트된 환자 수
        size_t patients_merged = 0;     // 병합된 환자 수
    };

    [[nodiscard]] statistics get_statistics() const;
    void reset_statistics();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::hl7
```

---

## 2. MLLP 전송 모듈

### 2.1 모듈 개요

**네임스페이스:** `pacs::bridge::mllp`
**목적:** MLLP 프로토콜 프레이밍 및 네트워크 전송 처리

### DES-MLLP-001: mllp_server

**추적 대상:** FR-1.3.1, FR-1.3.3, FR-1.3.4

```cpp
namespace pacs::bridge::mllp {

/**
 * @brief MLLP 서버 설정
 */
struct mllp_server_config {
    uint16_t port = 2575;
    size_t max_connections = 50;
    std::chrono::seconds connection_timeout{60};
    std::chrono::seconds receive_timeout{30};
    bool tls_enabled = false;
    std::string cert_path;
    std::string key_path;
};

/**
 * @brief 메시지 핸들러 콜백 타입
 *
 * @param message 수신된 HL7 메시지 (MLLP 프레임 제거됨)
 * @param connection 응답을 위한 연결 컨텍스트
 * @return 송신할 응답 메시지
 */
using message_handler = std::function<
    std::string(const std::string& message, mllp_connection& connection)>;

/**
 * @brief MLLP 서버 (리스너)
 *
 * MLLP 프레이밍을 사용하여 수신 HL7 연결을 수락.
 */
class mllp_server {
public:
    explicit mllp_server(const mllp_server_config& config);
    ~mllp_server();

    // 복사 불가, 이동 가능
    mllp_server(const mllp_server&) = delete;
    mllp_server& operator=(const mllp_server&) = delete;
    mllp_server(mllp_server&&) noexcept;
    mllp_server& operator=(mllp_server&&) noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // 핸들러 등록
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 메시지 핸들러 설정
     * @param handler 수신된 각 메시지에 대한 콜백
     */
    void set_handler(message_handler handler);

    /**
     * @brief 연결 이벤트 핸들러 설정
     */
    void set_connection_handler(
        std::function<void(const mllp_connection&, bool connected)> handler);

    // ═══════════════════════════════════════════════════════════════════
    // 생명주기
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 연결 수락 시작
     */
    [[nodiscard]] Result<void> start();

    /**
     * @brief 새 연결 수락 중지
     * @param wait_for_active 활성 연결 완료 대기 여부
     */
    void stop(bool wait_for_active = true);

    /**
     * @brief 서버 실행 중 여부 확인
     */
    [[nodiscard]] bool is_running() const noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // 통계
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 서버 통계 조회
     */
    struct statistics {
        size_t active_connections;   // 활성 연결 수
        size_t total_connections;    // 총 연결 수
        size_t messages_received;    // 수신 메시지 수
        size_t messages_sent;        // 송신 메시지 수
        size_t errors;               // 오류 수
    };

    [[nodiscard]] statistics get_statistics() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::mllp
```

### DES-MLLP-002: mllp_client

**추적 대상:** FR-1.3.2, FR-1.3.3

```cpp
namespace pacs::bridge::mllp {

/**
 * @brief MLLP 클라이언트 설정
 */
struct mllp_client_config {
    std::string host;
    uint16_t port = 2575;
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds send_timeout{30};
    std::chrono::seconds receive_timeout{30};
    bool tls_enabled = false;
    std::string ca_cert_path;
    bool keep_alive = true;
    size_t retry_count = 3;
    std::chrono::milliseconds retry_delay{1000};
};

/**
 * @brief MLLP 클라이언트 (송신자)
 *
 * 원격 MLLP 서버로 HL7 메시지 송신.
 */
class mllp_client {
public:
    explicit mllp_client(const mllp_client_config& config);
    ~mllp_client();

    // 복사 불가, 이동 가능
    mllp_client(const mllp_client&) = delete;
    mllp_client& operator=(const mllp_client&) = delete;
    mllp_client(mllp_client&&) noexcept;
    mllp_client& operator=(mllp_client&&) noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // 연결
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 원격 서버 연결
     */
    [[nodiscard]] Result<void> connect();

    /**
     * @brief 서버 연결 해제
     */
    void disconnect();

    /**
     * @brief 연결 상태 확인
     */
    [[nodiscard]] bool is_connected() const noexcept;

    // ═══════════════════════════════════════════════════════════════════
    // 메시징
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 메시지 송신 및 응답 대기
     * @param message HL7 메시지 (MLLP 프레이밍 없음)
     * @return 응답 메시지 또는 오류
     *
     * 오류:
     *   -920: MLLP_CONNECTION_FAILED (MLLP 연결 실패)
     *   -921: MLLP_SEND_FAILED (MLLP 송신 실패)
     *   -922: MLLP_RECEIVE_TIMEOUT (MLLP 수신 타임아웃)
     */
    [[nodiscard]] Result<std::string> send(const std::string& message);

    /**
     * @brief 비동기 메시지 송신
     * @param message HL7 메시지
     * @return 응답이 포함된 Future
     */
    [[nodiscard]] std::future<Result<std::string>>
        send_async(const std::string& message);

    /**
     * @brief 자동 재시도를 통한 송신
     */
    [[nodiscard]] Result<std::string> send_with_retry(
        const std::string& message,
        size_t max_retries = 0); // 0 = 설정 기본값 사용

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::mllp
```

### DES-MLLP-003: mllp_connection

**추적 대상:** FR-1.3.4

```cpp
namespace pacs::bridge::mllp {

/**
 * @brief MLLP 프레이밍 상수
 */
constexpr char MLLP_START = 0x0B;  // VT (Vertical Tab)
constexpr char MLLP_END_1 = 0x1C;  // FS (File Separator)
constexpr char MLLP_END_2 = 0x0D;  // CR (Carriage Return)

/**
 * @brief MLLP 연결 (서버 측)
 */
class mllp_connection {
public:
    /**
     * @brief 원격 엔드포인트 정보 조회
     */
    [[nodiscard]] std::string remote_address() const;
    [[nodiscard]] uint16_t remote_port() const;

    /**
     * @brief 연결 ID 조회
     */
    [[nodiscard]] std::string connection_id() const;

    /**
     * @brief 응답 송신
     * @param message HL7 메시지 (MLLP 프레이밍 자동 추가)
     */
    [[nodiscard]] Result<void> send(const std::string& message);

    /**
     * @brief 연결 종료
     */
    void close();

    /**
     * @brief 연결 열림 상태 확인
     */
    [[nodiscard]] bool is_open() const noexcept;

private:
    friend class mllp_server;
    class impl;
    std::shared_ptr<impl> pimpl_;
};

/**
 * @brief MLLP 프레임 유틸리티
 */
class mllp_frame {
public:
    /**
     * @brief 메시지에 MLLP 프레이밍 추가
     */
    [[nodiscard]] static std::string wrap(const std::string& message);

    /**
     * @brief 메시지에서 MLLP 프레이밍 제거
     * @return 메시지 내용 또는 유효하지 않은 프레임 시 오류
     */
    [[nodiscard]] static Result<std::string> unwrap(const std::string& frame);

    /**
     * @brief 버퍼에 완전한 MLLP 메시지가 있는지 확인
     * @param buffer 입력 버퍼
     * @param message_end 출력: 완전한 메시지 다음 위치 (발견 시)
     * @return 완전한 메시지 발견 시 true
     */
    [[nodiscard]] static bool is_complete(
        std::string_view buffer,
        size_t& message_end);
};

} // namespace pacs::bridge::mllp
```

---

## 3. FHIR 게이트웨이 모듈

### 3.1 모듈 개요

**네임스페이스:** `pacs::bridge::fhir`
**목적:** 최신 EHR 통합을 위한 FHIR R4 REST API 제공

### DES-FHIR-001: fhir_server

**추적 대상:** FR-2.1.1, FR-2.1.2, FR-2.1.3, FR-2.1.4

```cpp
namespace pacs::bridge::fhir {

/**
 * @brief FHIR 서버 설정
 */
struct fhir_server_config {
    uint16_t port = 8080;
    std::string base_path = "/fhir";
    bool tls_enabled = false;
    std::string cert_path;
    std::string key_path;
    size_t max_connections = 100;
    size_t page_size = 100;
};

/**
 * @brief FHIR R4 REST 서버
 */
class fhir_server {
public:
    explicit fhir_server(const fhir_server_config& config);
    ~fhir_server();

    // ═══════════════════════════════════════════════════════════════════
    // 리소스 핸들러
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 리소스 핸들러 등록
     * @tparam T 리소스 타입 (Patient, ServiceRequest 등)
     * @param handler 핸들러 인스턴스
     */
    template<typename T>
    void register_handler(std::shared_ptr<resource_handler<T>> handler);

    // ═══════════════════════════════════════════════════════════════════
    // 생명주기
    // ═══════════════════════════════════════════════════════════════════

    [[nodiscard]] Result<void> start();
    void stop();
    [[nodiscard]] bool is_running() const noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 * @brief 리소스 핸들러 인터페이스
 */
template<typename T>
class resource_handler {
public:
    virtual ~resource_handler() = default;

    /**
     * @brief ID로 리소스 읽기
     */
    [[nodiscard]] virtual Result<T> read(const std::string& id) = 0;

    /**
     * @brief 리소스 검색
     * @param params 검색 매개변수
     * @return 일치하는 리소스 Bundle
     */
    [[nodiscard]] virtual Result<std::vector<T>> search(
        const std::map<std::string, std::string>& params) = 0;

    /**
     * @brief 새 리소스 생성
     */
    [[nodiscard]] virtual Result<T> create(const T& resource) = 0;

    /**
     * @brief 기존 리소스 업데이트
     */
    [[nodiscard]] virtual Result<T> update(
        const std::string& id,
        const T& resource) = 0;

    /**
     * @brief 리소스 삭제
     */
    [[nodiscard]] virtual Result<void> delete_resource(
        const std::string& id) = 0;
};

} // namespace pacs::bridge::fhir
```

### DES-FHIR-002: fhir_resource

**추적 대상:** FR-2.2.1, FR-2.2.2, FR-2.2.3, FR-2.2.4

```cpp
namespace pacs::bridge::fhir {

/**
 * @brief 기본 FHIR 리소스
 */
class fhir_resource {
public:
    virtual ~fhir_resource() = default;

    [[nodiscard]] std::string id() const;
    [[nodiscard]] std::string resource_type() const;

    /**
     * @brief JSON으로 직렬화
     */
    [[nodiscard]] virtual std::string to_json() const = 0;

    /**
     * @brief JSON에서 파싱
     */
    [[nodiscard]] static Result<std::unique_ptr<fhir_resource>>
        from_json(const std::string& json);

protected:
    std::string id_;
    std::string resource_type_;
};

/**
 * @brief FHIR Patient 리소스
 */
class patient_resource : public fhir_resource {
public:
    struct name {
        std::string family;        // 성
        std::vector<std::string> given;  // 이름
    };

    struct identifier {
        std::string system;   // 식별자 시스템
        std::string value;    // 식별자 값
    };

    std::vector<identifier> identifiers;
    std::vector<name> names;
    std::string birth_date;
    std::string gender; // male, female, other, unknown

    [[nodiscard]] std::string to_json() const override;
    [[nodiscard]] static Result<patient_resource> from_json(
        const std::string& json);
};

/**
 * @brief FHIR ServiceRequest 리소스 (영상 오더)
 */
class service_request_resource : public fhir_resource {
public:
    std::string status; // draft, active, completed, cancelled
    std::string intent; // order, original-order
    std::string patient_reference;
    std::string requester_reference;

    struct coding {
        std::string system;
        std::string code;
        std::string display;
    };
    std::vector<coding> code_codings;

    std::string scheduled_datetime;
    std::string performer_reference; // AE Title / Location

    [[nodiscard]] std::string to_json() const override;
};

/**
 * @brief FHIR ImagingStudy 리소스
 */
class imaging_study_resource : public fhir_resource {
public:
    std::string status; // registered, available, cancelled
    std::string patient_reference;
    std::string study_instance_uid;
    std::string started;
    std::string description;
    size_t number_of_series = 0;
    size_t number_of_instances = 0;

    struct series_info {
        std::string uid;
        std::string modality;
        size_t number_of_instances;
    };
    std::vector<series_info> series;

    [[nodiscard]] std::string to_json() const override;
};

/**
 * @brief FHIR DiagnosticReport 리소스
 */
class diagnostic_report_resource : public fhir_resource {
public:
    std::string status; // registered, partial, preliminary, final
    std::string patient_reference;
    std::string study_reference;
    std::string conclusion;
    std::string issued;

    [[nodiscard]] std::string to_json() const override;
};

} // namespace pacs::bridge::fhir
```

### DES-FHIR-003: patient_resource_handler

**추적 대상:** FR-2.2.1

```cpp
namespace pacs::bridge::fhir {

/**
 * @brief FHIR Patient 리소스 핸들러
 *
 * 내부 환자 캐시(cache::patient_cache)에서 Patient 리소스를 매핑하여
 * 읽기 및 검색 작업을 구현합니다.
 *
 * 지원 작업:
 * - read: GET /Patient/{id}
 * - search: GET /Patient?identifier=xxx
 * - search: GET /Patient?name=xxx
 * - search: GET /Patient?birthdate=xxx
 */
class patient_resource_handler : public resource_handler {
public:
    explicit patient_resource_handler(
        std::shared_ptr<cache::patient_cache> cache);

    // ID로 환자 조회
    [[nodiscard]] resource_result<std::unique_ptr<fhir_resource>> read(
        const std::string& id) override;

    // 환자 검색
    // 지원 파라미터: _id, identifier, name, birthdate
    [[nodiscard]] resource_result<search_result> search(
        const std::map<std::string, std::string>& params,
        const pagination_params& pagination) override;

    // 지원되는 검색 파라미터 반환
    [[nodiscard]] std::map<std::string, std::string>
    supported_search_params() const override;

    // 인터랙션 지원 여부 확인
    [[nodiscard]] bool supports_interaction(
        interaction_type type) const noexcept override;

private:
    std::shared_ptr<cache::patient_cache> cache_;
};

/**
 * @brief DICOM 환자를 FHIR Patient 리소스로 변환
 */
[[nodiscard]] std::unique_ptr<patient_resource> dicom_to_fhir_patient(
    const mapping::dicom_patient& dicom_patient,
    const std::optional<std::string>& patient_id = std::nullopt);

/**
 * @brief DICOM 이름 형식을 FHIR HumanName으로 변환
 * DICOM PN 형식: Family^Given^Middle^Prefix^Suffix
 */
[[nodiscard]] fhir_human_name dicom_name_to_fhir(std::string_view dicom_name);

/**
 * @brief DICOM 날짜 형식을 FHIR 날짜 형식으로 변환
 * DICOM: YYYYMMDD -> FHIR: YYYY-MM-DD
 */
[[nodiscard]] std::string dicom_date_to_fhir(std::string_view dicom_date);

/**
 * @brief DICOM 성별 코드를 FHIR 성별로 변환
 * DICOM: M, F, O -> FHIR: male, female, other, unknown
 */
[[nodiscard]] administrative_gender dicom_sex_to_fhir_gender(
    std::string_view dicom_sex);

} // namespace pacs::bridge::fhir
```

---

## 4. 변환 계층 모듈

### 4.1 모듈 개요

**네임스페이스:** `pacs::bridge::mapping`
**목적:** HL7, FHIR, DICOM 프로토콜 간 변환

### DES-TRANS-001: hl7_dicom_mapper

**추적 대상:** FR-3.1.1, FR-3.1.2, FR-3.1.3, FR-3.1.4, FR-3.1.5

```cpp
namespace pacs::bridge::mapping {

/**
 * @brief HL7에서 DICOM MWL로의 매퍼
 *
 * HL7 ORM^O01 메시지를 DICOM Modality Worklist 항목으로 변환.
 */
class hl7_dicom_mapper {
public:
    explicit hl7_dicom_mapper(const mapping_config& config);

    // ═══════════════════════════════════════════════════════════════════
    // ORM → MWL 매핑
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief ORM 메시지를 MWL 데이터셋으로 변환
     * @param orm HL7 ORM^O01 메시지
     * @return DICOM MWL 데이터셋 또는 오류
     *
     * 매핑:
     *   PID-3 → PatientID (0010,0020)
     *   PID-5 → PatientName (0010,0010)
     *   PID-7 → PatientBirthDate (0010,0030)
     *   PID-8 → PatientSex (0010,0040)
     *   ORC-2 → PlacerOrderNumberImagingServiceRequest
     *   ORC-3 → AccessionNumber (0008,0050)
     *   OBR-4 → RequestedProcedureCodeSequence
     *   OBR-7 → ScheduledProcedureStepStartDate/Time
     *   OBR-24 → Modality (0008,0060)
     *   ZDS-1 → StudyInstanceUID (0020,000D)
     */
    [[nodiscard]] Result<pacs::core::dicom_dataset>
        orm_to_mwl(const hl7::hl7_message& orm);

    /**
     * @brief ADT 메시지를 환자 인구통계로 변환
     * @param adt HL7 ADT 메시지
     * @return 환자 인구통계 데이터셋
     */
    [[nodiscard]] Result<pacs::core::dicom_dataset>
        adt_to_patient(const hl7::hl7_message& adt);

    // ═══════════════════════════════════════════════════════════════════
    // 필드 수준 매핑
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 환자 이름 매핑 (HL7 XPN → DICOM PN)
     *
     * HL7 형식: FAMILY^GIVEN^MIDDLE^SUFFIX^PREFIX
     * DICOM 형식: FAMILY^GIVEN MIDDLE^PREFIX^SUFFIX
     */
    [[nodiscard]] static std::string map_patient_name(
        std::string_view hl7_name);

    /**
     * @brief 날짜 매핑 (HL7 → DICOM DA)
     *
     * HL7 형식: YYYYMMDD[HHMM[SS[.S[S[S[S]]]]]][+/-ZZZZ]
     * DICOM 형식: YYYYMMDD
     */
    [[nodiscard]] static std::string map_date(std::string_view hl7_date);

    /**
     * @brief 시간 매핑 (HL7 → DICOM TM)
     *
     * HL7 형식: HHMM[SS[.S[S[S[S]]]]]
     * DICOM 형식: HHMMSS.FFFFFF
     */
    [[nodiscard]] static std::string map_time(std::string_view hl7_time);

    /**
     * @brief 검사 코드 매핑
     *
     * RequestedProcedureCodeSequence 항목 생성
     */
    [[nodiscard]] Result<pacs::core::dicom_dataset>
        map_procedure_code(std::string_view hl7_code);

private:
    mapping_config config_;

    std::string lookup_modality_code(std::string_view procedure_code);
    std::string generate_uid();
};

/**
 * @brief 매핑 설정
 */
struct mapping_config {
    // 모달리티 코드 매핑
    std::map<std::string, std::string> procedure_to_modality;

    // 기관별 설정
    std::string institution_name;
    std::string station_ae_title_prefix;

    // UID 생성
    std::string uid_root;

    // 환자 ID 도메인
    std::string patient_id_issuer;
};

} // namespace pacs::bridge::mapping
```

### DES-TRANS-002: dicom_hl7_mapper

**추적 대상:** FR-3.2.1, FR-3.2.2, FR-3.2.3, FR-3.2.4, FR-3.2.5

```cpp
namespace pacs::bridge::mapping {

/**
 * @brief DICOM에서 HL7로의 매퍼
 *
 * MPPS 이벤트를 HL7 ORM 상태 업데이트 및 ORU 메시지로 변환.
 */
class dicom_hl7_mapper {
public:
    explicit dicom_hl7_mapper(const mapping_config& config);

    // ═══════════════════════════════════════════════════════════════════
    // MPPS → HL7 매핑
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief MPPS IN PROGRESS를 HL7 ORM 상태 업데이트로 변환
     * @param mpps MPPS N-CREATE 데이터셋
     * @return 상태 업데이트가 포함된 HL7 ORM^O01 메시지
     *
     * 매핑:
     *   PerformedProcedureStepStatus → ORC-5 (IP = In Progress)
     *   PerformedStationAETitle → OBR-21
     *   PerformedProcedureStepStartDateTime → OBR-22
     */
    [[nodiscard]] Result<std::string>
        mpps_in_progress_to_orm(const pacs::core::dicom_dataset& mpps);

    /**
     * @brief MPPS COMPLETED를 HL7 ORM 상태 업데이트로 변환
     * @param mpps MPPS N-SET 데이터셋
     * @return 완료 상태가 포함된 HL7 ORM^O01 메시지
     *
     * 매핑:
     *   PerformedProcedureStepStatus → ORC-5 (CM = Complete)
     *   PerformedProcedureStepEndDateTime → OBR-27
     *   PerformedSeriesSequence → (로깅 전용)
     */
    [[nodiscard]] Result<std::string>
        mpps_completed_to_orm(const pacs::core::dicom_dataset& mpps);

    /**
     * @brief MPPS DISCONTINUED를 HL7 ORM 취소로 변환
     * @param mpps DISCONTINUED 상태의 MPPS N-SET 데이터셋
     * @return 취소가 포함된 HL7 ORM^O01 메시지
     */
    [[nodiscard]] Result<std::string>
        mpps_discontinued_to_orm(const pacs::core::dicom_dataset& mpps);

    // ═══════════════════════════════════════════════════════════════════
    // 리포트 → ORU 매핑
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 리포트를 HL7 ORU^R01로 변환
     * @param study 검사 정보
     * @param report 리포트 내용
     * @param status 리포트 상태 (P = Preliminary, F = Final)
     * @return HL7 ORU^R01 메시지
     */
    [[nodiscard]] Result<std::string> report_to_oru(
        const pacs::core::dicom_dataset& study,
        std::string_view report,
        char status);

private:
    mapping_config config_;

    std::string format_datetime(
        std::string_view dicom_date,
        std::string_view dicom_time);
};

/**
 * @brief MPPS 상태에서 HL7 오더 제어/상태로의 매핑
 */
struct mpps_status_mapping {
    static constexpr std::string_view orc_order_control(
        std::string_view mpps_status) {
        if (mpps_status == "IN PROGRESS") return "SC";  // Status Change
        if (mpps_status == "COMPLETED") return "SC";
        if (mpps_status == "DISCONTINUED") return "CA"; // Cancel
        return "SC";
    }

    static constexpr std::string_view orc_order_status(
        std::string_view mpps_status) {
        if (mpps_status == "IN PROGRESS") return "IP";
        if (mpps_status == "COMPLETED") return "CM";
        if (mpps_status == "DISCONTINUED") return "CA";
        return "";
    }
};

} // namespace pacs::bridge::mapping
```

### DES-TRANS-003: fhir_dicom_mapper

**추적 대상:** FR-2.2.2, FR-2.2.3
**상태:** 구현 완료 ([Issue #35](https://github.com/kcenon/pacs_bridge/issues/35))
**헤더:** `include/pacs/bridge/mapping/fhir_dicom_mapper.h`
**소스:** `src/mapping/fhir_dicom_mapper.cpp`
**테스트:** `tests/fhir_dicom_mapper_test.cpp` (50개 테스트)

```cpp
namespace pacs::bridge::mapping {

/**
 * @brief FHIR-DICOM 양방향 매퍼
 *
 * FHIR R4 리소스와 DICOM 데이터 구조 간의 양방향 매핑을 제공합니다.
 * MWL(Modality Worklist) 및 검사 쿼리를 지원합니다.
 *
 * 지원 매핑:
 *   - FHIR ServiceRequest -> DICOM MWL 예약 프로시저 단계
 *   - DICOM Study -> FHIR ImagingStudy
 *   - FHIR Patient <-> DICOM Patient (양방향)
 *
 * 기능:
 *   - LOINC에서 DICOM 프로시저 코드 변환
 *   - SNOMED에서 DICOM 신체 부위 코드 변환
 *   - 자동 UID 생성
 *   - 날짜/시간 형식 변환 (FHIR <-> DICOM)
 *   - 우선순위 매핑 (routine/urgent/stat <-> MEDIUM/HIGH/STAT)
 */
class fhir_dicom_mapper {
public:
    using patient_lookup_function =
        std::function<std::expected<dicom_patient, fhir_dicom_error>(
            const std::string&)>;

    fhir_dicom_mapper();
    explicit fhir_dicom_mapper(const fhir_dicom_mapper_config& config);

    // ServiceRequest를 MWL로 변환
    [[nodiscard]] std::expected<mwl_item, fhir_dicom_error>
    service_request_to_mwl(const fhir_service_request& request,
                           const dicom_patient& patient) const;

    // DICOM Study를 ImagingStudy로 변환
    [[nodiscard]] std::expected<fhir_imaging_study, fhir_dicom_error>
    study_to_imaging_study(
        const dicom_study& study,
        const std::optional<std::string>& patient_reference = std::nullopt) const;

    // 환자 매핑 (양방향)
    [[nodiscard]] std::expected<std::unique_ptr<fhir::patient_resource>, fhir_dicom_error>
    dicom_to_fhir_patient(const dicom_patient& dicom_patient) const;

    [[nodiscard]] std::expected<dicom_patient, fhir_dicom_error>
    fhir_to_dicom_patient(const fhir::patient_resource& patient) const;

    // 코드 시스템 변환
    [[nodiscard]] std::optional<fhir_coding> loinc_to_dicom(const std::string& loinc_code) const;
    [[nodiscard]] std::optional<fhir_coding> snomed_to_dicom(const std::string& snomed_code) const;

    // 유틸리티 함수
    [[nodiscard]] std::string generate_uid(const std::string& suffix = "") const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::mapping
```

#### FHIR에서 DICOM으로의 필드 매핑

**ServiceRequest → DICOM MWL:**

| FHIR 요소 | DICOM 태그 | 설명 |
|-----------|-----------|------|
| subject.reference | PatientID (0010,0020) | 환자 식별자 |
| code.coding[0].code | (0008,0100) CodeValue | 프로시저 코드 |
| code.coding[0].display | (0008,0104) CodeMeaning | 프로시저 설명 |
| occurrenceDateTime | (0040,0002/0003) 예약 시작일/시간 | 예약 일시 |
| performer[0].reference | (0040,0010) ScheduledStationAETitle | 대상 장비 |
| priority | (0040,1003) RequestedProcedurePriority | 주문 우선순위 |

**DICOM Study → ImagingStudy:**

| DICOM 태그 | FHIR 요소 | 설명 |
|-----------|-----------|------|
| (0020,000D) StudyInstanceUID | identifier[0].value | 검사 고유 ID |
| (0008,0020/0030) StudyDate/Time | started | 검사 시작 시간 |
| (0008,0050) AccessionNumber | identifier[1].value | 접수 번호 |
| (0008,1030) StudyDescription | description | 검사 설명 |

#### 우선순위 매핑

| FHIR 우선순위 | DICOM 우선순위 |
|---------------|----------------|
| stat | STAT |
| urgent | HIGH |
| routine | MEDIUM |

---

## 5. 메시지 라우팅 모듈

### 5.1 모듈 개요

**네임스페이스:** `pacs::bridge::router`
**목적:** 게이트웨이 간 메시지 라우팅 및 메시지 큐잉 처리

### DES-ROUTE-001: message_router

**추적 대상:** FR-4.1.1, FR-4.1.2, FR-4.1.3, FR-4.2.1, FR-4.2.2

```cpp
namespace pacs::bridge::router {

/**
 * @brief 라우팅 규칙
 */
struct routing_rule {
    std::string name;
    std::string message_type_pattern;  // 예: "ADT^A*", "ORM^O01"
    std::string source_pattern;        // 예: "HIS_*"
    std::string destination;           // 핸들러 이름 또는 엔드포인트
    int priority = 0;                  // 높을수록 우선순위 높음
};

/**
 * @brief 메시지 라우터
 *
 * 규칙에 따라 수신 메시지를 적절한 핸들러로 라우팅.
 */
class message_router {
public:
    explicit message_router(const std::vector<routing_rule>& rules);

    // ═══════════════════════════════════════════════════════════════════
    // 핸들러 등록
    // ═══════════════════════════════════════════════════════════════════

    using handler_fn = std::function<Result<void>(const hl7::hl7_message&)>;

    /**
     * @brief 메시지 핸들러 등록
     * @param name 핸들러 이름 (라우팅 규칙에서 참조)
     * @param handler 핸들러 함수
     */
    void register_handler(std::string_view name, handler_fn handler);

    // ═══════════════════════════════════════════════════════════════════
    // 라우팅
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 수신 메시지 라우팅
     * @param message 라우팅할 HL7 메시지
     * @return 핸들러 실행 결과
     */
    [[nodiscard]] Result<void> route(const hl7::hl7_message& message);

    /**
     * @brief 메시지에 대한 일치 규칙 조회
     * @param message HL7 메시지
     * @return 일치하는 규칙 또는 nullptr
     */
    [[nodiscard]] const routing_rule* find_rule(
        const hl7::hl7_message& message) const;

private:
    std::vector<routing_rule> rules_;
    std::map<std::string, handler_fn> handlers_;

    bool matches_pattern(std::string_view value, std::string_view pattern);
};

} // namespace pacs::bridge::router
```

### DES-ROUTE-002: queue_manager

**추적 대상:** FR-4.3.1, FR-4.3.2, FR-4.3.3, FR-4.3.4

```cpp
namespace pacs::bridge::router {

/**
 * @brief 큐에 저장된 메시지
 */
struct queued_message {
    std::string id;
    std::string destination;
    std::string payload;
    int priority = 0;
    std::chrono::system_clock::time_point created;
    std::chrono::system_clock::time_point next_attempt;
    int attempt_count = 0;
    std::string last_error;
};

/**
 * @brief 큐 설정
 */
struct queue_config {
    std::string database_path = "queue.db";
    size_t max_queue_size = 50000;
    size_t max_retry_count = 5;
    std::chrono::seconds initial_retry_delay{5};
    double retry_backoff_multiplier = 2.0;
    std::chrono::seconds max_retry_delay{300};
    std::chrono::hours message_ttl{24};

    // 선택적 IExecutor - worker/cleanup 태스크 실행용
    // nullptr일 경우 내부 std::thread 사용 (standalone 모드)
    std::shared_ptr<kcenon::common::interfaces::IExecutor> executor;
};

/**
 * @brief 영속적 메시지 큐 관리자
 */
class queue_manager {
public:
    explicit queue_manager(const queue_config& config);
    ~queue_manager();

    // ═══════════════════════════════════════════════════════════════════
    // 큐 연산
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 전달을 위한 메시지 큐잉
     * @param destination 목적지 식별자
     * @param payload 메시지 페이로드
     * @param priority 메시지 우선순위 (높을수록 긴급)
     * @return 메시지 ID 또는 큐 가득 찬 경우 오류
     */
    [[nodiscard]] Result<std::string> enqueue(
        std::string_view destination,
        std::string_view payload,
        int priority = 0);

    /**
     * @brief 전달을 위한 다음 메시지 디큐
     * @param destination 디큐할 목적지
     * @return 메시지 또는 큐 비어있는 경우 nullopt
     */
    [[nodiscard]] std::optional<queued_message> dequeue(
        std::string_view destination);

    /**
     * @brief 메시지 성공적 전달 표시
     */
    Result<void> ack(std::string_view message_id);

    /**
     * @brief 메시지 실패 표시 (나중에 재시도)
     * @param message_id 메시지 ID
     * @param error 오류 설명
     */
    Result<void> nack(std::string_view message_id, std::string_view error);

    /**
     * @brief 메시지 영구 실패 (더 이상 재시도 안 함)
     */
    Result<void> dead_letter(std::string_view message_id,
                             std::string_view reason);

    // ═══════════════════════════════════════════════════════════════════
    // 큐 상태
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 큐 깊이 조회
     */
    [[nodiscard]] size_t queue_depth(std::string_view destination) const;

    /**
     * @brief 전체 대기 메시지 수 조회
     */
    [[nodiscard]] size_t total_pending() const;

    /**
     * @brief Dead letter 수 조회
     */
    [[nodiscard]] size_t dead_letter_count() const;

    // ═══════════════════════════════════════════════════════════════════
    // 백그라운드 처리
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 백그라운드 전달 워커 시작
     * @param sender 메시지 송신 함수
     */
    void start_workers(
        std::function<Result<void>(const queued_message&)> sender);

    /**
     * @brief 백그라운드 워커 중지
     */
    void stop_workers();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace pacs::bridge::router
```

---

## 6. pacs_system 어댑터 모듈

### 6.1 모듈 개요

**네임스페이스:** `pacs::bridge::pacs_adapter`
**목적:** DICOM 서비스를 위한 pacs_system 통합

### DES-PACS-001: mwl_client

**추적 대상:** IR-1 (worklist_scp), FR-3.1

```cpp
namespace pacs::bridge::pacs_adapter {

/**
 * @brief MWL 클라이언트 설정
 */
struct mwl_client_config {
    std::string pacs_host = "localhost";
    uint16_t pacs_port = 11112;
    std::string our_ae_title = "PACS_BRIDGE";
    std::string pacs_ae_title = "PACS_SCP";
    std::chrono::seconds timeout{30};
};

/**
 * @brief Modality Worklist 클라이언트
 *
 * pacs_system에서 MWL 항목 관리.
 */
class mwl_client {
public:
    explicit mwl_client(const mwl_client_config& config);
    ~mwl_client();

    // ═══════════════════════════════════════════════════════════════════
    // MWL 연산
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 새 워크리스트 항목 추가
     * @param mwl_item MWL 데이터셋 (HL7 ORM 매핑에서)
     */
    [[nodiscard]] Result<void> add_entry(
        const pacs::core::dicom_dataset& mwl_item);

    /**
     * @brief 기존 워크리스트 항목 업데이트
     * @param accession_number 업데이트할 Accession 번호
     * @param updates 업데이트할 필드
     */
    [[nodiscard]] Result<void> update_entry(
        std::string_view accession_number,
        const pacs::core::dicom_dataset& updates);

    /**
     * @brief 워크리스트 항목 취소 (제거)
     * @param accession_number 취소할 Accession 번호
     */
    [[nodiscard]] Result<void> cancel_entry(std::string_view accession_number);

    /**
     * @brief 워크리스트 항목 조회
     * @param query 쿼리 키
     * @return 일치하는 항목
     */
    [[nodiscard]] Result<std::vector<pacs::core::dicom_dataset>> query(
        const pacs::core::dicom_dataset& query);

    /**
     * @brief 항목 존재 여부 확인
     */
    [[nodiscard]] bool exists(std::string_view accession_number);

private:
    mwl_client_config config_;
    // 내부적으로 pacs::services::worklist_scp 사용
};

} // namespace pacs::bridge::pacs_adapter
```

### DES-PACS-002: mpps_handler

**추적 대상:** IR-1 (mpps_scp), FR-3.2

```cpp
namespace pacs::bridge::pacs_adapter {

/**
 * @brief MPPS 이벤트 타입
 */
enum class mpps_event {
    in_progress,   // 검사 진행 중
    completed,     // 검사 완료
    discontinued   // 검사 중단
};

/**
 * @brief MPPS 이벤트 콜백
 */
using mpps_callback = std::function<void(
    mpps_event event,
    const pacs::core::dicom_dataset& mpps)>;

/**
 * @brief MPPS 핸들러
 *
 * pacs_system에서 MPPS 알림을 수신하여 HL7로 전달.
 */
class mpps_handler {
public:
    explicit mpps_handler(const mpps_callback& callback);

    /**
     * @brief pacs_system MPPS SCP에 등록
     * @param mpps_scp pacs_system MPPS SCP 참조
     */
    void register_with_pacs(pacs::services::mpps_scp& mpps_scp);

    /**
     * @brief 수신 MPPS N-CREATE 처리
     */
    void on_n_create(const pacs::core::dicom_dataset& mpps);

    /**
     * @brief 수신 MPPS N-SET 처리
     */
    void on_n_set(const pacs::core::dicom_dataset& mpps);

private:
    mpps_callback callback_;
};

} // namespace pacs::bridge::pacs_adapter
```

### DES-PACS-003: patient_cache

**추적 대상:** FR-4.1.1

```cpp
namespace pacs::bridge::pacs_adapter {

/**
 * @brief 환자 캐시 설정
 */
struct patient_cache_config {
    size_t max_entries = 10000;
    std::chrono::seconds ttl{3600};  // 기본값 1시간
    bool evict_on_full = true;
};

/**
 * @brief 환자 인구통계
 */
struct patient_info {
    std::string patient_id;
    std::string patient_name;
    std::string birth_date;
    std::string sex;
    std::string issuer;
    std::chrono::system_clock::time_point cached_at;
};

/**
 * @brief 인메모리 환자 캐시
 *
 * 빠른 조회를 위해 ADT 메시지의 환자 인구통계를 캐시.
 * 읽기-쓰기 잠금을 통한 스레드 안전.
 */
class patient_cache {
public:
    explicit patient_cache(const patient_cache_config& config);

    // ═══════════════════════════════════════════════════════════════════
    // 캐시 연산
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 환자 추가 또는 업데이트
     * @param patient 환자 정보
     */
    void put(const patient_info& patient);

    /**
     * @brief ID로 환자 조회
     * @param patient_id 환자 ID
     * @return 환자 정보 또는 찾지 못하거나/만료된 경우 nullopt
     */
    [[nodiscard]] std::optional<patient_info> get(
        std::string_view patient_id) const;

    /**
     * @brief 캐시에서 환자 제거
     */
    void remove(std::string_view patient_id);

    /**
     * @brief 모든 항목 삭제
     */
    void clear();

    // ═══════════════════════════════════════════════════════════════════
    // 상태
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief 캐시 크기 조회
     */
    [[nodiscard]] size_t size() const noexcept;

    /**
     * @brief 히트율 조회
     */
    [[nodiscard]] double hit_rate() const noexcept;

    /**
     * @brief 캐시 통계 조회
     */
    struct statistics {
        size_t entries;     // 항목 수
        size_t hits;        // 히트 수
        size_t misses;      // 미스 수
        size_t evictions;   // 퇴거 수
    };
    [[nodiscard]] statistics get_statistics() const;

private:
    patient_cache_config config_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, patient_info> cache_;
    std::list<std::string> lru_order_;  // LRU 퇴거용

    mutable size_t hits_ = 0;
    mutable size_t misses_ = 0;
    size_t evictions_ = 0;

    void evict_if_needed();
    void touch(std::string_view patient_id);
};

} // namespace pacs::bridge::pacs_adapter
```

---

## 7. 설정 모듈

### DES-CFG-001: bridge_config

**추적 대상:** FR-5.1, FR-5.2

```cpp
namespace pacs::bridge::config {

/**
 * @brief 전체 브릿지 설정
 */
struct bridge_config {
    // ═══════════════════════════════════════════════════════════════════
    // 서버 식별
    // ═══════════════════════════════════════════════════════════════════

    std::string name = "PACS_BRIDGE";

    // ═══════════════════════════════════════════════════════════════════
    // HL7 게이트웨이
    // ═══════════════════════════════════════════════════════════════════

    struct hl7_config {
        mllp::mllp_server_config listener;
        std::vector<mllp::mllp_client_config> outbound_destinations;
    } hl7;

    // ═══════════════════════════════════════════════════════════════════
    // FHIR 게이트웨이
    // ═══════════════════════════════════════════════════════════════════

    struct fhir_config {
        bool enabled = false;
        fhir::fhir_server_config server;
    } fhir;

    // ═══════════════════════════════════════════════════════════════════
    // pacs_system 연결
    // ═══════════════════════════════════════════════════════════════════

    struct pacs_config {
        std::string host = "localhost";
        uint16_t port = 11112;
        std::string ae_title = "PACS_BRIDGE";
        std::string called_ae = "PACS_SCP";
    } pacs;

    // ═══════════════════════════════════════════════════════════════════
    // 매핑 설정
    // ═══════════════════════════════════════════════════════════════════

    mapping::mapping_config mapping;

    // ═══════════════════════════════════════════════════════════════════
    // 라우팅 설정
    // ═══════════════════════════════════════════════════════════════════

    std::vector<router::routing_rule> routing_rules;

    // ═══════════════════════════════════════════════════════════════════
    // 큐 설정
    // ═══════════════════════════════════════════════════════════════════

    router::queue_config queue;

    // ═══════════════════════════════════════════════════════════════════
    // 캐시 설정
    // ═══════════════════════════════════════════════════════════════════

    pacs_adapter::patient_cache_config patient_cache;

    // ═══════════════════════════════════════════════════════════════════
    // 로깅 설정
    // ═══════════════════════════════════════════════════════════════════

    struct logging_config {
        std::string level = "INFO";
        std::string format = "json";
        std::string file;
        std::string audit_file;
    } logging;
};

/**
 * @brief 설정 로더
 */
class config_loader {
public:
    /**
     * @brief YAML 파일에서 설정 로드
     */
    [[nodiscard]] static Result<bridge_config> load_yaml(
        const std::filesystem::path& path);

    /**
     * @brief JSON 파일에서 설정 로드
     */
    [[nodiscard]] static Result<bridge_config> load_json(
        const std::filesystem::path& path);

    /**
     * @brief 설정 검증
     */
    [[nodiscard]] static Result<void> validate(const bridge_config& config);

    /**
     * @brief 설정을 파일로 저장
     */
    [[nodiscard]] static Result<void> save_yaml(
        const bridge_config& config,
        const std::filesystem::path& path);
};

} // namespace pacs::bridge::config
```

---

## 8. 통합 모듈

### DES-INT-001: network_adapter

**추적 대상:** IR-2 (network_system)

```cpp
namespace pacs::bridge::integration {

/**
 * @brief 네트워크 시스템 어댑터
 *
 * TCP/TLS 연산을 위한 network_system 래핑.
 */
class network_adapter {
public:
    /**
     * @brief TCP 서버 생성
     */
    [[nodiscard]] static std::unique_ptr<network_system::tcp_server>
        create_tcp_server(uint16_t port, size_t max_connections);

    /**
     * @brief TCP 클라이언트 연결 생성
     */
    [[nodiscard]] static Result<network_system::tcp_socket>
        connect(const std::string& host, uint16_t port,
                std::chrono::milliseconds timeout);

    /**
     * @brief TLS 컨텍스트 설정
     */
    static void configure_tls(
        network_system::tls_context& ctx,
        const std::filesystem::path& cert_path,
        const std::filesystem::path& key_path);
};

} // namespace pacs::bridge::integration
```

### DES-INT-002: thread_adapter

**추적 대상:** IR-2 (thread_system)

```cpp
namespace pacs::bridge::integration {

/**
 * @brief 스레드 시스템 어댑터
 *
 * 비동기 처리를 위한 워커 풀 제공.
 */
class thread_adapter {
public:
    /**
     * @brief 공유 IO 스레드 풀 가져오기
     */
    [[nodiscard]] static thread_system::thread_pool& get_io_pool();

    /**
     * @brief 메시지 처리 풀 가져오기
     */
    [[nodiscard]] static thread_system::thread_pool& get_worker_pool();

    /**
     * @brief 비동기 작업 제출
     */
    template<typename F>
    [[nodiscard]] static auto submit(F&& job);

    /**
     * @brief 지연 작업 스케줄링
     */
    template<typename F>
    static void schedule(F&& job, std::chrono::milliseconds delay);
};

} // namespace pacs::bridge::integration
```

### DES-INT-003: logger_adapter

**추적 대상:** IR-2 (logger_system)

```cpp
namespace pacs::bridge::integration {

/**
 * @brief 로거 시스템 어댑터
 *
 * 브릿지 연산을 위한 구조화된 로깅 제공.
 */
class logger_adapter {
public:
    /**
     * @brief HL7 메시지 로깅
     */
    static void log_message(
        const hl7::hl7_message& message,
        std::string_view direction); // "IN" 또는 "OUT"

    /**
     * @brief 감사 이벤트 로깅
     */
    static void log_audit(
        std::string_view event_type,
        std::string_view patient_id,
        std::string_view details);

    /**
     * @brief 오류 로깅
     */
    static void log_error(
        std::string_view component,
        int error_code,
        std::string_view message);

    /**
     * @brief 컴포넌트 로거 가져오기
     */
    [[nodiscard]] static logger_system::logger& get_logger(
        std::string_view component);
};

} // namespace pacs::bridge::integration
```

### DES-INT-004: executor_adapter

**추적 대상:** IR-2 (common_system), Issue #198, Issue #210, Issue #228

```cpp
namespace pacs::bridge::integration {

/**
 * @brief IExecutor 어댑터 구현
 *
 * common_system의 IExecutor 인터페이스와 pacs_bridge의 스레드 인프라를
 * 연결하는 어댑터를 제공합니다. 워크플로우 모듈이 기존 스레드 풀 구현을
 * 활용하면서 표준화된 IExecutor 인터페이스를 사용할 수 있게 합니다.
 *
 * 통합된 컴포넌트 (Issue #208, #228):
 * - config_manager: 파일 감시자가 주기적 검사에 IExecutor 사용
 * - batch_exporter: 내보내기 루프가 작업 스케줄링에 IExecutor 사용
 * - mpps_hl7_workflow: 비동기 처리를 위해 워커 태스크에 IExecutor 사용
 * - hl7_message_bus: 메시지 처리를 위한 선택적 IExecutor
 * - messaging_backend: 메시지 버스 생성을 위한 IExecutor 팩토리 지원
 *
 * @see https://github.com/kcenon/pacs_bridge/issues/198
 * @see https://github.com/kcenon/pacs_bridge/issues/210
 * @see https://github.com/kcenon/pacs_bridge/issues/228
 */

/**
 * @class lambda_job
 * @brief 호출 가능한 객체를 래핑하는 IJob 구현
 */
class lambda_job : public kcenon::common::interfaces::IJob {
public:
    explicit lambda_job(
        std::function<kcenon::common::VoidResult()> func,
        std::string name = "lambda_job",
        int priority = 0);

    kcenon::common::VoidResult execute() override;
    [[nodiscard]] std::string get_name() const override;
    [[nodiscard]] int get_priority() const override;
};

/**
 * @class simple_executor
 * @brief 내부 스레드 풀을 가진 경량 IExecutor 구현
 *
 * 스레드 안전성 (Issue #229):
 * - 모든 public 메서드는 스레드-안전합니다
 * - shutdown()은 compare_exchange_strong을 사용하여 동시 종료 충돌 방지
 * - 여러 스레드에서 동시에 shutdown() 호출 시 안전
 */
class simple_executor : public kcenon::common::interfaces::IExecutor {
public:
    explicit simple_executor(std::size_t worker_count = std::thread::hardware_concurrency());

    [[nodiscard]] kcenon::common::Result<std::future<void>> execute(
        std::unique_ptr<kcenon::common::interfaces::IJob>&& job) override;

    [[nodiscard]] kcenon::common::Result<std::future<void>> execute_delayed(
        std::unique_ptr<kcenon::common::interfaces::IJob>&& job,
        std::chrono::milliseconds delay) override;

    [[nodiscard]] std::size_t worker_count() const override;
    [[nodiscard]] bool is_running() const override;
    [[nodiscard]] std::size_t pending_tasks() const override;

    /**
     * @brief Executor 종료
     *
     * 스레드-안전: atomic compare_exchange_strong을 사용하여
     * 하나의 스레드만 종료를 수행하도록 보장, double-join 충돌 방지
     *
     * @param wait_for_completion true면 대기 중인 작업 완료까지 대기
     */
    void shutdown(bool wait_for_completion = true) override;
};

/**
 * @class thread_pool_executor_adapter
 * @brief kcenon::thread::thread_pool을 사용하는 IExecutor 구현
 */
class thread_pool_executor_adapter : public kcenon::common::interfaces::IExecutor {
public:
    explicit thread_pool_executor_adapter(
        std::shared_ptr<kcenon::thread::thread_pool> pool);

    // ... IExecutor 인터페이스 구현
};

/**
 * @brief 워커 수로 executor를 생성하는 팩토리 함수
 */
[[nodiscard]] std::shared_ptr<kcenon::common::interfaces::IExecutor>
make_executor(std::size_t worker_count = std::thread::hardware_concurrency());

/**
 * @brief 기존 스레드 풀에서 executor를 생성하는 팩토리 함수
 */
[[nodiscard]] std::shared_ptr<kcenon::common::interfaces::IExecutor>
make_executor(std::shared_ptr<kcenon::thread::thread_pool> pool);

} // namespace pacs::bridge::integration
```

---

*문서 버전: 0.2.0.0*
*작성일: 2025-12-07*
*작성자: kcenon@naver.com*
