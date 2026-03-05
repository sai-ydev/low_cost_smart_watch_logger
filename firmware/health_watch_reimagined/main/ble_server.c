#include "ble_server.h"
#include "rv8803.h"
#include "esp_log.h"
#include "nvs_flash.h"

// NimBLE headers
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE";

// ── Connection state ──────────────────────────────────────────────────────────
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool     connected   = false;

// ── Characteristic value handles (needed for notifications) ──────────────────
static uint16_t hr_handle;
static uint16_t spo2_handle;
static uint16_t temp_handle;
static uint16_t steps_handle;
static uint16_t imu_handle;

// ── Current values ────────────────────────────────────────────────────────────
static int32_t  cur_hr        = 0;
static int32_t  cur_spo2      = 0;
static float    cur_temp      = 0.0f;
static uint16_t cur_steps     = 0;
static ble_imu_t cur_imu      = {0};

// ── GATT read callbacks ───────────────────────────────────────────────────────
// Forward declaration
static void ble_server_start_advertising(void);

static int gatt_read_hr(uint16_t conn, uint16_t attr,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return os_mbuf_append(ctxt->om, &cur_hr, sizeof(cur_hr));
}

static int gatt_read_spo2(uint16_t conn, uint16_t attr,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return os_mbuf_append(ctxt->om, &cur_spo2, sizeof(cur_spo2));
}

static int gatt_read_temp(uint16_t conn, uint16_t attr,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return os_mbuf_append(ctxt->om, &cur_temp, sizeof(cur_temp));
}

static int gatt_read_steps(uint16_t conn, uint16_t attr,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return os_mbuf_append(ctxt->om, &cur_steps, sizeof(cur_steps));
}

static int gatt_read_imu(uint16_t conn, uint16_t attr,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return os_mbuf_append(ctxt->om, &cur_imu, sizeof(cur_imu));
}

// ── DateTime read/write callback ──────────────────────────────────────────────

static int gatt_datetime_cb(uint16_t conn, uint16_t attr,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Android reading current time from RTC
        rv8803_time_t t = {0};
        rv8803_get_time(&t);
        ble_datetime_t dt = {
            .year    = t.year,
            .month   = t.month,
            .date    = t.date,
            .hours   = t.hours,
            .minutes = t.minutes,
            .seconds = t.seconds
        };
        return os_mbuf_append(ctxt->om, &dt, sizeof(dt));

    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // Android setting time on RTC
        ble_datetime_t dt = {0};
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

        if (len != sizeof(ble_datetime_t)) {
            ESP_LOGE(TAG, "DateTime write wrong length: %d", len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        ble_hs_mbuf_to_flat(ctxt->om, &dt, sizeof(dt), NULL);

        rv8803_time_t t = {
            .year    = dt.year,
            .month   = dt.month,
            .date    = dt.date,
            .hours   = dt.hours,
            .minutes = dt.minutes,
            .seconds = dt.seconds
        };
        rv8803_set_time(&t);
        ESP_LOGI(TAG, "RTC set via BLE: %04d-%02d-%02d %02d:%02d:%02d",
                 t.year, t.month, t.date, t.hours, t.minutes, t.seconds);
    }
    return 0;
}

// ── GATT service table ────────────────────────────────────────────────────────

static const struct ble_gatt_svc_def gatt_services[] = {

    // ── Health Service ────────────────────────────────────────────────────────
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(HEALTH_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {

            // Heart Rate
            {
                .uuid       = BLE_UUID16_DECLARE(CHAR_HEART_RATE_UUID),
                .access_cb  = gatt_read_hr,
                .val_handle = &hr_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },

            // SpO2
            {
                .uuid       = BLE_UUID16_DECLARE(CHAR_SPO2_UUID),
                .access_cb  = gatt_read_spo2,
                .val_handle = &spo2_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },

            // Temperature
            {
                .uuid       = BLE_UUID16_DECLARE(CHAR_TEMPERATURE_UUID),
                .access_cb  = gatt_read_temp,
                .val_handle = &temp_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },

            // Steps
            {
                .uuid       = BLE_UUID16_DECLARE(CHAR_STEPS_UUID),
                .access_cb  = gatt_read_steps,
                .val_handle = &steps_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },

            // IMU
            {
                .uuid       = BLE_UUID16_DECLARE(CHAR_IMU_UUID),
                .access_cb  = gatt_read_imu,
                .val_handle = &imu_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },

            { 0 } // terminator
        },
    },

    // ── Time Service ──────────────────────────────────────────────────────────
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(TIME_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {

            // DateTime — readable and writable
            {
                .uuid      = BLE_UUID16_DECLARE(CHAR_DATETIME_UUID),
                .access_cb = gatt_datetime_cb,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },

            { 0 } // terminator
        },
    },

    { 0 } // terminator
};


// ── GAP event handler ─────────────────────────────────────────────────────────

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                conn_handle = event->connect.conn_handle;
                connected   = true;
                ESP_LOGI(TAG, "Client connected — handle: %d", conn_handle);
                ESP_LOGI(TAG, "Handles: HR=%d SpO2=%d Temp=%d Steps=%d IMU=%d",
                        hr_handle, spo2_handle, temp_handle,
                        steps_handle, imu_handle);
            } else {
                ESP_LOGE(TAG, "Connection failed — restarting advertising");
                connected = false;
                ble_server_start_advertising();
            }
            break;

        // ... rest unchanged
    }
    return 0;
}

// ── Advertising ───────────────────────────────────────────────────────────────

static void ble_server_start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};

    fields.flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl            = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    const char *name = ble_svc_gap_device_name();
    fields.name      = (uint8_t *)name;
    fields.name_len  = strlen(name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // undirected connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // general discoverable

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                                &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Advertising start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising started — device name: %s", name);
    }
}

// ── NimBLE host task ──────────────────────────────────────────────────────────

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();  // blocks here until nimble_port_stop()
    nimble_port_freertos_deinit();
}

// ── Sync callback — called when BLE stack is ready ───────────────────────────

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE reset — reason: %d", reason);
}

static void ble_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_server_start_advertising();
}
// ── Init ──────────────────────────────────────────────────────────────────────

esp_err_t ble_server_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    nimble_port_init();

    ble_att_set_preferred_mtu(64);


    // Must be called before ble_gatts_add_svcs
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_services);
    if (rc != 0) { ESP_LOGE(TAG, "count cfg failed: %d", rc); return ESP_FAIL; }

    rc = ble_gatts_add_svcs(gatt_services);
    if (rc != 0) { ESP_LOGE(TAG, "add svcs failed: %d", rc); return ESP_FAIL; }

    ble_svc_gap_device_name_set("HealthWatch");

    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE server initialised");
    return ESP_OK;
}
// ── Public update functions ───────────────────────────────────────────────────

bool ble_is_connected(void) { return connected; }

static void notify(uint16_t handle, const void *data, uint16_t len)
{
    if (!connected) return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return;

    int rc = ble_gatts_notify_custom(conn_handle, handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Notify failed on handle %d: %d", handle, rc);
    }
}

void ble_update_heart_rate(int32_t bpm)
{
    cur_hr = bpm;
    notify(hr_handle, &cur_hr, sizeof(cur_hr));
}

void ble_update_spo2(int32_t spo2)
{
    cur_spo2 = spo2;
    notify(spo2_handle, &cur_spo2, sizeof(cur_spo2));
}

void ble_update_temperature(float temp)
{
    cur_temp = temp;
    notify(temp_handle, &cur_temp, sizeof(cur_temp));
}

void ble_update_steps(uint16_t steps)
{
    cur_steps = steps;
    notify(steps_handle, &cur_steps, sizeof(cur_steps));
}

void ble_update_imu(float ax, float ay, float az,
                    float gx, float gy, float gz)
{
    cur_imu.accel_x = ax; cur_imu.accel_y = ay; cur_imu.accel_z = az;
    cur_imu.gyro_x  = gx; cur_imu.gyro_y  = gy; cur_imu.gyro_z  = gz;
    notify(imu_handle, &cur_imu, sizeof(cur_imu));
}

