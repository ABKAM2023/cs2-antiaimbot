## RU
Anti-Aimbot - плагин для автоматического выявления и бана игроков, использующих anti-aim, silent-aim и aimbot (отдельные разновидности aimbot могут не определяться). Плагин не создаёт заметной нагрузки на сервер. В ходе тестирования ложных банов не выявлено; теоретически они маловероятны. Если столкнётесь с ошибочным баном, пожалуйста, сообщите в Discord: https://discord.gg/ChYfTtrtmS

## Требования
- [Utils](https://github.com/Pisex/cs2-menus/releases)
- [Admin System](https://github.com/Pisex/cs2-admin_system/releases)

## Конфиг
```ini
"AntiAimbot"
{
    "debug"                       "0"      // логи в консоль (0/1)
    "sample_interval"             "0.03"   // период записи прицела, сек
    "analysis_window_s"           "0.60"   // окно анализа вокруг килла, сек

    "fov_lock_deg"                "4.0"    // FOV «в цель», °
    "lock_var_deg"                "0.50"   // доп. дрожь в lock, °/шаг
    "lock_hold_s"                 "0.25"   // мин. длительность lock, сек

    "snap_delta_deg"              "12.0"   // порог флика, °
    "pre_snap_min_fov"            "10.0"   // мин. FOV до флика, °

    "silent_fov_min"              "20.0"   // FOV до жертвы для silent, °
    "silent_weight"               "18.0"   // вес silent-сигнала
    "max_shot_age"                "0.35"   // макс. время с выстрела до килла, сек
    "head_bonus"                  "2.0"    // множитель за хедшот (silent)

    "lock_weight"                 "10.0"   // вес lock-сигнала
    "snap_weight"                 "10.0"   // вес snap-сигнала
    "threshold"                   "20.0"   // порог бана (очки)

    "close_range_m"               "3.0"    // ближняя дистанция, м
    "close_range_mult"            "0.50"   // ослабление очков на ближней
    "decay_per_second"            "1.0"    // спад подозрения, очк/сек

    "approach_window_s"           "0.40"   // окно «человеч. подвода», сек
    "approach_need_frac"          "0.60"   // доля времени с уменьшением FOV
    "approach_min_time"           "0.12"   // мин. активного времени, сек
    "approach_bonus"              "8.0"    // сколько снять при естеств. подводе, очков

    "continuity_window_s"         "0.50"   // окно «флик→лок», сек
    "continuity_weight"           "6.0"    // вес непрерывности
    "continuity_max_spike_deg"    "9.0"    // порог разового всплеска Δ, °
    "continuity_min_avg_delta"    "0.20"   // верхний предел средней Δ (≤ °)

    "buf_hard_cap"                "9000"   // кап буфера сэмплов, шт
    "buf_decimate_factor"         "2"      // прореживание старой половины

    "crouch_eyez_delta_min"       "14.0"   // мин. Δ по eyeZ для приседа/спуска, юниты
    "vertical_dom_ratio"          "0.75"   // доля вертикальных движений для «верт. доминанты»
    "snap_near_boost_per_m"       "8.0"    // +° к порогу снапа за метр до close_range_m
    "lock_close_mult"             "0.50"   // ослабление lock на ближней
    "snap_vertical_boost_per_deg" "0.5"    // +° к порогу снапа за градус вертик. угла
    "headshot_elev_no_bonus_deg"  "15.0"   // ≥ этого угла хедшот без бонуса

    "lock_acq_lookback_s"         "0.25"   // сколько смотреть назад перед входом, сек
    "lock_acq_min_delta_deg"      "2.0"    // мин. сумм./пиковое Δ до входа, °
    "lock_enter_fov_margin_deg"   "0.5"    // запас к fov_lock для «входа из-вне», °
    "lock_angle_hold_reduce"      "0.15"   // срез веса lock при angle-hold, ×
    "angle_hold_prefov_deg"       "1.2"    // если prevFOV ≤ … считаем «держал угол», °
    "angle_hold_maxdelta_deg"     "1.0"    // и preMaxΔ ≤ … — обнуляем lock, °

    "exclude_warmup"              "0"      // игнорировать разминку (0/1)
    "ban_time_minutes"            "0"      // время бана (0=перма)
    "reason"                      "Использование Aimbot" // причина бана
}
```

## EN
Anti-Aimbot - is a server-side plugin that automatically detects and bans players using anti-aim, silent-aim, and many aimbot variants (some edge cases may evade detection). It adds negligible server overhead. No false bans were observed during testing; in theory they should be unlikely. If you encounter a false positive, please report it in Discord: [https://discord.gg/ChYfTtrtmS](https://discord.gg/ChYfTtrtmS)

## Requirements
* [Utils](https://github.com/Pisex/cs2-menus/releases)
* [Admin System](https://github.com/Pisex/cs2-admin_system/releases)

## Config
```ini
"AntiAimbot"
{
    "debug"                       "0"      // console logs (0/1)
    "sample_interval"             "0.03"   // crosshair sampling period, s
    "analysis_window_s"           "0.60"   // analysis window around the kill, s

    "fov_lock_deg"                "4.0"    // FOV “on target”, °
    "lock_var_deg"                "0.50"   // extra jitter inside a lock, °/tick
    "lock_hold_s"                 "0.25"   // minimum lock duration, s

    "snap_delta_deg"              "12.0"   // flick threshold, °
    "pre_snap_min_fov"            "10.0"   // minimum pre-flick FOV, °

    "silent_fov_min"              "20.0"   // attacker FOV to victim for silent-aim, °
    "silent_weight"               "18.0"   // silent-aim signal weight
    "max_shot_age"                "0.35"   // max time from shot to kill, s
    "head_bonus"                  "2.0"    // multiplier for headshot (silent)

    "lock_weight"                 "10.0"   // lock signal weight
    "snap_weight"                 "10.0"   // snap signal weight
    "threshold"                   "20.0"   // ban threshold (points)

    "close_range_m"               "3.0"    // close-range distance, m
    "close_range_mult"            "0.50"   // score attenuation at close range
    "decay_per_second"            "1.0"    // suspicion decay, pts/s

    "approach_window_s"           "0.40"   // “human aim-in” window, s
    "approach_need_frac"          "0.60"   // fraction of time with decreasing FOV
    "approach_min_time"           "0.12"   // minimum active time, s
    "approach_bonus"              "8.0"    // deduction for natural approach, pts

    "continuity_window_s"         "0.50"   // “flick→lock” continuity window, s
    "continuity_weight"           "6.0"    // continuity weight
    "continuity_max_spike_deg"    "9.0"    // max single Δ spike, °
    "continuity_min_avg_delta"    "0.20"   // upper bound for average Δ (≤ °)

    "buf_hard_cap"                "9000"   // sample buffer hard cap, entries
    "buf_decimate_factor"         "2"      // decimate the older half by factor

    "crouch_eyez_delta_min"       "14.0"   // min eyeZ Δ for crouch/step down, units
    "vertical_dom_ratio"          "0.75"   // share of vertical motion for “vertical dominance”
    "snap_near_boost_per_m"       "8.0"    // +° to snap threshold per meter down to close_range_m
    "lock_close_mult"             "0.50"   // lock weakening at close range
    "snap_vertical_boost_per_deg" "0.5"    // +° to snap threshold per degree of vertical angle
    "headshot_elev_no_bonus_deg"  "15.0"   // ≥ this elevation angle: no headshot bonus

    "lock_acq_lookback_s"         "0.25"   // lookback before lock acquisition, s
    "lock_acq_min_delta_deg"      "2.0"    // minimum cumulative/peak Δ before entry, °
    "lock_enter_fov_margin_deg"   "0.5"    // margin to fov_lock for “entering from outside”, °
    "lock_angle_hold_reduce"      "0.15"   // reduce lock weight on angle-hold, ×
    "angle_hold_prefov_deg"       "1.2"    // if prevFOV ≤ … treat as “holding an angle”, °
    "angle_hold_maxdelta_deg"     "1.0"    // and preMaxΔ ≤ … — nullify lock, °

    "exclude_warmup"              "0"      // ignore warmup (0/1)
    "ban_time_minutes"            "0"      // ban duration (0=permanent)
    "reason"                      "Aimbot usage" // ban reason
}
```
