import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import './App.css'

const STORAGE_KEY = 'm5-marine-dashboard-layout-v4'
const CONTROL_KEY = 'm5-marine-dashboard-controls-v1'
const COMMAND_COOLDOWN_MS = 900
const CONFIRM_WINDOW_MS = 2500
const STALE_MS = 3000
const GRID_UNIT_X = 85
const GRID_UNIT_Y = 118
const MAX_WIDGET_HEIGHT = 8

const widgetCatalog = [
  { id: 'heroInstruments', label: 'Large Heading', type: 'special', defaultSize: { w: 6, h: 2 } },
  { id: 'heading', label: 'Heading', type: 'metric', unit: 'deg', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'awa', label: 'Apparent Wind Angle', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'sog', label: 'SOG', type: 'metric', unit: 'kt', precision: 1, defaultSize: { w: 2, h: 1 } },
  { id: 'cog', label: 'COG', type: 'metric', unit: 'deg', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'depth', label: 'Depth', type: 'metric', unit: 'm', precision: 1, defaultSize: { w: 2, h: 1 } },
  { id: 'waterTemp', label: 'Water Temp', type: 'metric', unit: 'C', precision: 1, defaultSize: { w: 2, h: 1 } },
  { id: 'rudderAngle', label: 'Rudder Angle', type: 'metric', unit: 'deg', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'battery0', label: 'Battery 0', type: 'metric', unit: 'V', precision: 2, defaultSize: { w: 2, h: 1 } },
  { id: 'battery1', label: 'Battery 1', type: 'metric', unit: 'V', precision: 2, defaultSize: { w: 2, h: 1 } },
  { id: 'batteryDiff', label: 'Battery Diff', type: 'metric', unit: 'V', precision: 2, defaultSize: { w: 2, h: 1 } },
  { id: 'autopilotMode', label: 'Autopilot Mode', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'targetHeading', label: 'Pilot Heading', type: 'metric', unit: 'deg', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'windTargetDisplay', label: 'Wind Target', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'seatalkStatus', label: 'SeaTalk 1', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'n2kStatus', label: 'N2K', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'windStatus', label: 'Wind Data', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'wifiClients', label: 'Wi-Fi Clients', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'electricalWarnings', label: 'Electrical Warnings', type: 'special', defaultSize: { w: 4, h: 1 } },
  { id: 'autopilotPanel', label: 'Autopilot Controls', type: 'special', defaultSize: { w: 8, h: 4 } },
  { id: 'windOffsetPanel', label: 'Wind Offset', type: 'special', defaultSize: { w: 4, h: 2 } },
  { id: 'rawSeatalk', label: 'Raw SeaTalk 1', type: 'special', defaultSize: { w: 4, h: 2 } },
  { id: 'decodedSeatalk', label: 'Decoded SeaTalk 1', type: 'special', defaultSize: { w: 4, h: 2 } },
  { id: 'rawN2k', label: 'Raw N2K PGNs', type: 'special', defaultSize: { w: 4, h: 2 } },
  { id: 'packetCounters', label: 'Packet Counters', type: 'special', defaultSize: { w: 4, h: 2 } },
]

const widgetById = Object.fromEntries(widgetCatalog.map((widget) => [widget.id, widget]))

const defaultPages = [
  createPage('helm', 'Helm', [
    'heroInstruments',
    'awa',
    'sog',
    'cog',
    'depth',
    'rudderAngle',
    'autopilotMode',
    'targetHeading',
    'windTargetDisplay',
    'seatalkStatus',
    'autopilotPanel',
    'windOffsetPanel',
  ]),
  createPage('sailing', 'Sailing', ['awa', 'heading', 'sog', 'cog', 'depth', 'waterTemp', 'rudderAngle']),
  createPage('electrical', 'Electrical', ['battery0', 'battery1', 'batteryDiff', 'electricalWarnings', 'wifiClients']),
  createPage('debug', 'Debug', ['seatalkStatus', 'n2kStatus', 'windStatus', 'rawSeatalk', 'decodedSeatalk', 'rawN2k', 'packetCounters']),
]

const defaultLayout = {
  pages: defaultPages,
  activePageId: 'helm',
}

const blankData = {
  plcOnline: false,
  heading: null,
  cog: null,
  sog: null,
  awa: null,
  awaDisplay: null,
  awaRaw: null,
  windOffset: null,
  windStatus: null,
  windLastSeenMs: null,
  windTargetAngle: null,
  windTargetDisplay: null,
  depth: null,
  waterTemp: null,
  rudderAngle: null,
  battery0: null,
  battery1: null,
  autopilotMode: null,
  targetHeading: null,
  seatalkStatus: null,
  n2kStatus: null,
  seatalkLastSeenMs: null,
  n2kLastSeenMs: null,
  wifiClients: null,
  uptime: null,
  packetCounters: {
    seatalkRaw: null,
    seatalkDecoded: null,
    n2kPgn: null,
    errors: null,
  },
  rawSeatalk: [],
  decodedSeatalk: [],
  rawN2k: [],
}

function createPage(id, name, widgets = []) {
  return {
    id,
    name,
    widgets,
    sizes: Object.fromEntries(
      widgets.map((widgetId) => [widgetId, widgetById[widgetId]?.defaultSize || { w: 2, h: 1 }]),
    ),
  }
}

function normalizePage(page, fallbackIndex = 0) {
  const knownIds = new Set(widgetCatalog.map((widget) => widget.id))
  const widgets = Array.isArray(page.widgets) ? page.widgets.filter((id) => knownIds.has(id)) : []
  return {
    id: page.id || `page-${fallbackIndex + 1}`,
    name: page.name || `Page ${fallbackIndex + 1}`,
    widgets,
    sizes: widgets.reduce((sizes, widgetId) => {
      sizes[widgetId] = normalizeSize(page.sizes?.[widgetId] || widgetById[widgetId]?.defaultSize)
      return sizes
    }, {}),
  }
}

function normalizeSize(size) {
  return {
    w: Math.min(8, Math.max(1, Number(size?.w) || 2)),
    h: Math.min(MAX_WIDGET_HEIGHT, Math.max(1, Number(size?.h) || 1)),
  }
}

function formatWidth(widthUnits) {
  return (widthUnits / 2).toFixed(widthUnits % 2 === 0 ? 0 : 1)
}

function loadLayout() {
  try {
    const saved = JSON.parse(localStorage.getItem(STORAGE_KEY))
    if (!Array.isArray(saved?.pages) || saved.pages.length === 0) return defaultLayout
    const pages = saved.pages.map(normalizePage)
    const activePageId = pages.some((page) => page.id === saved.activePageId) ? saved.activePageId : pages[0].id
    return { pages, activePageId }
  } catch {
    return defaultLayout
  }
}

function formatValue(value, precision = 0) {
  if (value === null || value === undefined || Number.isNaN(value)) return '--'
  if (typeof value === 'number') return value.toFixed(precision)
  return value
}

function clampHeading(value) {
  return Math.round((value + 360) % 360)
}

function normalizeSignedDegrees(value) {
  if (typeof value !== 'number' || Number.isNaN(value)) return null
  let degrees = ((value % 360) + 360) % 360
  if (degrees > 180) degrees -= 360
  return degrees
}

function formatRelativeWindAngle(value) {
  if (value === null || value === undefined || Number.isNaN(value)) return '--'
  let rounded = Math.round(((value % 360) + 360) % 360)
  if (rounded >= 360) rounded -= 360
  if (rounded === 0) return '0'
  if (rounded === 180) return '180'
  return rounded < 180 ? `${rounded}S` : `${360 - rounded}P`
}

function formatAge(value) {
  return typeof value === 'number' && Number.isFinite(value) ? `${value} ms` : '--'
}

function normalizeIncomingData(nextData) {
  return {
    ...blankData,
    ...nextData,
    plcOnline: true,
    packetCounters: {
      ...blankData.packetCounters,
      ...(nextData.packetCounters || {}),
    },
    rawSeatalk: Array.isArray(nextData.rawSeatalk) ? nextData.rawSeatalk : [],
    decodedSeatalk: Array.isArray(nextData.decodedSeatalk) ? nextData.decodedSeatalk : [],
    rawN2k: Array.isArray(nextData.rawN2k) ? nextData.rawN2k : [],
  }
}

function useMarineData() {
  const [data, setData] = useState(blankData)

  useEffect(() => {
    let cancelled = false

    async function pollApi() {
      const controller = new AbortController()
      const timeout = window.setTimeout(() => controller.abort(), 700)

      try {
        const response = await fetch('/data', {
          cache: 'no-store',
          signal: controller.signal,
        })
        if (!response.ok) throw new Error(`Data endpoint returned ${response.status}`)
        const nextData = await response.json()
        if (!cancelled) {
          setData(normalizeIncomingData(nextData))
        }
      } catch {
        if (!cancelled) {
          setData(blankData)
        }
      } finally {
        window.clearTimeout(timeout)
      }
    }

    pollApi()

    const interval = window.setInterval(() => {
      pollApi()
    }, 1000)

    return () => {
      cancelled = true
      window.clearInterval(interval)
    }
  }, [])

  return [data, setData]
}

function StatusStrip({ data }) {
  const plcOffline = !data.plcOnline
  const seatalkStale = plcOffline || data.seatalkLastSeenMs > STALE_MS
  const n2kStale = plcOffline || data.n2kLastSeenMs > STALE_MS
  const windStale = plcOffline || data.windStatus !== 'ok'

  return (
    <section className="status-strip" aria-label="Network status">
      <div>
        <span className={plcOffline ? 'status-dot bad' : 'status-dot'} />
        PLC {plcOffline ? 'offline' : 'online'}
      </div>
      <div>
        <span className={seatalkStale ? 'status-dot bad' : 'status-dot'} />
        SeaTalk 1 {formatValue(data.seatalkStatus)} · {formatAge(data.seatalkLastSeenMs)}
      </div>
      <div>
        <span className={n2kStale ? 'status-dot bad' : 'status-dot'} />
        N2K {formatValue(data.n2kStatus)} · {formatAge(data.n2kLastSeenMs)}
      </div>
      <div>
        <span className={windStale ? 'status-dot bad' : 'status-dot'} />
        Wind {formatValue(data.windStatus)} · {formatAge(data.windLastSeenMs)}
      </div>
      <div>AP clients {formatValue(data.wifiClients)}</div>
    </section>
  )
}

function AutoFitText({ value, unit = '', className = '', min = 18, max = 260 }) {
  const boxRef = useRef(null)
  const textRef = useRef(null)

  useLayoutEffect(() => {
    const box = boxRef.current
    const text = textRef.current
    if (!box || !text) return undefined

    function fit() {
      const availableWidth = box.clientWidth - 2
      const availableHeight = box.clientHeight - 2
      if (availableWidth <= 0 || availableHeight <= 0) return

      let low = min
      let high = max
      text.style.whiteSpace = 'nowrap'

      for (let index = 0; index < 12; index += 1) {
        const midpoint = (low + high) / 2
        text.style.fontSize = `${midpoint}px`
        if (text.scrollWidth <= availableWidth && text.scrollHeight <= availableHeight) {
          low = midpoint
        } else {
          high = midpoint
        }
      }

      text.style.fontSize = `${Math.floor(low)}px`
    }

    fit()
    const observer = new ResizeObserver(fit)
    observer.observe(box)
    window.addEventListener('resize', fit)

    return () => {
      observer.disconnect()
      window.removeEventListener('resize', fit)
    }
  }, [value, unit, min, max])

  return (
    <div className="fit-text-box" ref={boxRef}>
      <strong className={`fit-text ${className}`} ref={textRef}>
        {value}
        {unit && <span>{unit}</span>}
      </strong>
    </div>
  )
}

function DashboardTile({ widget, size, children, onResize }) {
  const resizeStart = useRef(null)

  function beginResize(event) {
    event.preventDefault()
    event.currentTarget.setPointerCapture(event.pointerId)
    resizeStart.current = {
      x: event.clientX,
      y: event.clientY,
      size,
    }
  }

  function moveResize(event) {
    if (!resizeStart.current) return
    const deltaW = Math.round((event.clientX - resizeStart.current.x) / GRID_UNIT_X)
    const deltaH = Math.round((event.clientY - resizeStart.current.y) / GRID_UNIT_Y)
    onResize(widget.id, {
      w: Math.min(8, Math.max(1, resizeStart.current.size.w + deltaW)),
      h: Math.min(MAX_WIDGET_HEIGHT, Math.max(1, resizeStart.current.size.h + deltaH)),
    })
  }

  function endResize() {
    resizeStart.current = null
  }

  return (
    <div
      className="dashboard-tile"
      style={{
        '--tile-w': size.w,
        '--tile-w-mobile': Math.min(size.w, 2),
        '--tile-h': size.h,
      }}
    >
      {children}
      <button
        type="button"
        className="resize-grip"
        aria-label={`Resize ${widget.label}`}
        onPointerDown={beginResize}
        onPointerMove={moveResize}
        onPointerUp={endResize}
        onPointerCancel={endResize}
      />
    </div>
  )
}

function MetricCard({ widget, data }) {
  let value = data[widget.id]
  let unit = widget.unit

  if (widget.id === 'batteryDiff') {
    value = typeof data.battery0 === 'number' && typeof data.battery1 === 'number' ? data.battery0 - data.battery1 : null
  } else if (widget.id === 'awa') {
    value = data.awaDisplay || formatRelativeWindAngle(data.awa)
    unit = ''
  }

  const isVoltageWarning =
    (widget.id === 'battery0' || widget.id === 'battery1') && typeof value === 'number' && value < 12.2
  const statusClass = value === 'ok' ? 'good' : value === 'warn' ? 'warn' : ''

  return (
    <article className={`metric-card ${isVoltageWarning ? 'warning' : ''}`}>
      <span className="metric-label">{widget.label}</span>
      <AutoFitText
        className={statusClass}
        value={formatValue(value, widget.precision)}
        unit={unit}
        min={20}
        max={360}
      />
    </article>
  )
}

function HeroInstruments({ data }) {
  return (
    <section className="instrument-hero" aria-label="Primary instruments">
      <div className="hero-heading">
        <span>Heading</span>
        <AutoFitText value={formatValue(data.heading, 0)} unit="deg" min={44} max={420} />
      </div>
      <div className="hero-stack">
        <div>
          <span>AWA</span>
          <AutoFitText value={data.awaDisplay || formatRelativeWindAngle(data.awa)} min={18} max={180} />
        </div>
        <div>
          <span>SOG / COG</span>
          <AutoFitText value={`${formatValue(data.sog, 1)} / ${formatValue(data.cog, 0)}`} unit="kt/deg" min={18} max={180} />
        </div>
        <div>
          <span>Depth</span>
          <AutoFitText value={formatValue(data.depth, 1)} unit="m" min={18} max={180} />
        </div>
      </div>
    </section>
  )
}

function useCommandCooldown() {
  const [cooldownActive, setCooldownActive] = useState(false)
  const cooldownTimer = useRef(null)

  function startCooldown() {
    setCooldownActive(true)
    cooldownTimer.current = window.setTimeout(() => {
      setCooldownActive(false)
      cooldownTimer.current = null
    }, COMMAND_COOLDOWN_MS)
  }

  useEffect(() => {
    return () => {
      if (cooldownTimer.current) window.clearTimeout(cooldownTimer.current)
    }
  }, [])

  return { cooldownActive, startCooldown }
}

function AutopilotPanel({ data, setData, controlsEnabled, setControlsEnabled }) {
  const [armedCommand, setArmedCommand] = useState('')
  const [commandLog, setCommandLog] = useState([])
  const confirmTimer = useRef(null)
  const { cooldownActive, startCooldown } = useCommandCooldown()
  const disabledByStale = data.seatalkStatus !== 'ok' || data.seatalkLastSeenMs > STALE_MS
  const controlsLocked = !controlsEnabled || disabledByStale || cooldownActive

  useEffect(() => {
    localStorage.setItem(CONTROL_KEY, JSON.stringify({ controlsEnabled }))
  }, [controlsEnabled])

  useEffect(() => {
    return () => {
      if (confirmTimer.current) window.clearTimeout(confirmTimer.current)
    }
  }, [])

  function appendLog(command) {
    setCommandLog((entries) => [{ at: new Date().toLocaleTimeString(), command }, ...entries.slice(0, 4)])
  }

  function sendCommand(command, value) {
    if (cooldownActive) return
    startCooldown()
    appendLog(value === undefined ? command : `${command} ${value}`)

    fetch('/api/autopilot', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(value === undefined ? { command } : { command, value }),
    }).catch(() => {
      // PC development can run without the ESP32 command endpoint.
    })

    setData((previous) => {
      if (command === 'standby') return { ...previous, autopilotMode: 'STANDBY' }
      if (command === 'auto') return { ...previous, autopilotMode: 'AUTO' }
      if (command === 'wind') return { ...previous, autopilotMode: 'WIND' }
      if (command === 'track') return { ...previous, autopilotMode: 'TRACK' }
      if (
        (command === 'tack_port' || command === 'tack_starboard') &&
        previous.autopilotMode === 'WIND' &&
        typeof previous.windTargetAngle === 'number'
      ) {
        const windTargetAngle = ((360 - previous.windTargetAngle) % 360 + 360) % 360
        return {
          ...previous,
          windTargetAngle,
          windTargetDisplay: formatRelativeWindAngle(windTargetAngle),
        }
      }
      if (command === 'heading_delta') {
        const updates = {}
        if (typeof previous.targetHeading === 'number') {
          updates.targetHeading = clampHeading(previous.targetHeading + value)
        }
        if (previous.autopilotMode === 'WIND' && typeof previous.windTargetAngle === 'number') {
          const windTargetAngle = ((previous.windTargetAngle + value) % 360 + 360) % 360
          updates.windTargetAngle = windTargetAngle
          updates.windTargetDisplay = formatRelativeWindAngle(windTargetAngle)
        }
        return { ...previous, ...updates }
      }
      return previous
    })
  }

  function confirmCommand(command) {
    if (controlsLocked) return
    if (armedCommand === command) {
      if (confirmTimer.current) window.clearTimeout(confirmTimer.current)
      confirmTimer.current = null
      setArmedCommand('')
      sendCommand(command)
      return
    }

    setArmedCommand(command)
    if (confirmTimer.current) window.clearTimeout(confirmTimer.current)
    confirmTimer.current = window.setTimeout(() => {
      setArmedCommand('')
      confirmTimer.current = null
    }, CONFIRM_WINDOW_MS)
  }

  const standbyDisabled = !controlsEnabled || disabledByStale || cooldownActive

  return (
    <section className="pilot-panel" aria-label="Autopilot controls">
      <div className="panel-header">
        <div>
          <span className="eyebrow">Autopilot</span>
          <h2>{formatValue(data.autopilotMode)}</h2>
        </div>
        <label className="control-toggle">
          <input
            type="checkbox"
            checked={controlsEnabled}
            onChange={(event) => setControlsEnabled(event.target.checked)}
          />
          Controls
        </label>
      </div>

      {disabledByStale && <div className="safety-message">Pilot commands disabled: SeaTalk 1 data is stale.</div>}

      <div className="pilot-readouts">
        <div>
          <span>Pilot Heading</span>
          <strong>{formatValue(data.targetHeading, 0)}{typeof data.targetHeading === 'number' ? ' deg' : ''}</strong>
        </div>
        <div>
          <span>Wind Target</span>
          <strong>{formatValue(data.windTargetDisplay)}</strong>
        </div>
      </div>

      <button
        type="button"
        className="pilot-button standby"
        disabled={standbyDisabled}
        onClick={() => sendCommand('standby')}
      >
        STANDBY
      </button>

      <div className="pilot-grid mode-grid">
        {['auto', 'wind', 'track'].map((command) => (
          <ConfirmButton
            key={command}
            command={command}
            disabled={controlsLocked}
            armed={armedCommand === command}
            onConfirm={confirmCommand}
          />
        ))}
      </div>

      <div className="pilot-grid nudge-grid">
        {[
          ['-10', -10],
          ['-1', -1],
          ['+1', 1],
          ['+10', 10],
        ].map(([label, value]) => (
          <button
            type="button"
            key={label}
            className="pilot-button"
            disabled={controlsLocked}
            onClick={() => sendCommand('heading_delta', value)}
          >
            {label} deg
          </button>
        ))}
      </div>

      <div className="pilot-grid tack-grid">
        <ConfirmButton
          command="tack_port"
          label="Tack Port"
          disabled={controlsLocked}
          armed={armedCommand === 'tack_port'}
          onConfirm={confirmCommand}
        />
        <ConfirmButton
          command="tack_starboard"
          label="Tack Starboard"
          disabled={controlsLocked}
          armed={armedCommand === 'tack_starboard'}
          onConfirm={confirmCommand}
        />
      </div>

      <div className="command-log" aria-live="polite">
        {commandLog.length === 0
          ? 'No commands sent this session'
          : commandLog.map((entry) => <span key={`${entry.at}-${entry.command}`}>{entry.at} {entry.command}</span>)}
      </div>
    </section>
  )
}

function WindOffsetPanel({ data, setData, controlsEnabled }) {
  const [commandLog, setCommandLog] = useState([])
  const { cooldownActive, startCooldown } = useCommandCooldown()
  const windConfigDisabled = !controlsEnabled || !data.plcOnline || cooldownActive
  const windOffsetText = typeof data.windOffset === 'number' ? `${data.windOffset.toFixed(1)} deg` : '--'
  const rawText = typeof data.awaRaw === 'number' ? `${data.awaRaw.toFixed(1)} deg` : '--'
  const adjustedText = data.awaDisplay || formatRelativeWindAngle(data.awa)

  function appendLog(command) {
    setCommandLog((entries) => [{ at: new Date().toLocaleTimeString(), command }, ...entries.slice(0, 3)])
  }

  function applyWindConfig(body, label, optimisticOffset = null) {
    if (windConfigDisabled) return
    startCooldown()
    appendLog(label)

    if (typeof optimisticOffset === 'number') {
      setData((previous) => ({
        ...previous,
        windOffset: optimisticOffset,
        awa:
          typeof previous.awaRaw === 'number'
            ? ((previous.awaRaw + optimisticOffset) % 360 + 360) % 360
            : previous.awa,
        awaDisplay:
          typeof previous.awaRaw === 'number'
            ? formatRelativeWindAngle(previous.awaRaw + optimisticOffset)
            : previous.awaDisplay,
      }))
    }

    fetch('/api/wind-config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
      .then((response) => {
        if (!response.ok) throw new Error(`Wind config returned ${response.status}`)
        return response.json()
      })
      .then((config) => {
        setData((previous) => ({
          ...previous,
          windOffset: typeof config.offset === 'number' ? config.offset : previous.windOffset,
          awa: typeof config.adjustedAngle === 'number' ? config.adjustedAngle : previous.awa,
          awaDisplay: config.display ?? previous.awaDisplay,
          windStatus: config.status ?? previous.windStatus,
          windTargetAngle:
            typeof config.windTargetAngle === 'number' ? config.windTargetAngle : previous.windTargetAngle,
          windTargetDisplay: config.windTargetDisplay ?? previous.windTargetDisplay,
        }))
      })
      .catch(() => {
        // The next /data poll will restore the authoritative PLC state.
      })
  }

  function nudgeWindOffset(delta) {
    const currentOffset = typeof data.windOffset === 'number' ? data.windOffset : 0
    const nextOffset = normalizeSignedDegrees(currentOffset + delta) ?? 0
    applyWindConfig({ offset: nextOffset }, `offset ${nextOffset.toFixed(1)}`, nextOffset)
  }

  function zeroWindToBow() {
    const nextOffset = typeof data.awaRaw === 'number' ? normalizeSignedDegrees(-data.awaRaw) : null
    applyWindConfig({ zeroToBow: true }, 'zero bow', nextOffset)
  }

  return (
    <section className="wind-offset-panel" aria-label="Wind offset controls">
      <div className="panel-header compact">
        <div>
          <span className="eyebrow">Wind Offset</span>
          <h2>{windOffsetText}</h2>
        </div>
      </div>

      <div className="wind-offset-readouts">
        <div><span>AWA</span><strong>{adjustedText}</strong></div>
        <div><span>Raw</span><strong>{rawText}</strong></div>
      </div>

      <div className="pilot-grid wind-offset-grid">
        {[[-10, '-10'], [-1, '-1'], [1, '+1'], [10, '+10']].map(([delta, label]) => (
          <button
            type="button"
            key={label}
            className="pilot-button"
            disabled={windConfigDisabled}
            onClick={() => nudgeWindOffset(delta)}
          >
            {label}
          </button>
        ))}
        <button
          type="button"
          className="pilot-button"
          disabled={windConfigDisabled || typeof data.awaRaw !== 'number'}
          onClick={zeroWindToBow}
        >
          Zero Bow
        </button>
      </div>

      <div className="command-log" aria-live="polite">
        {commandLog.length === 0
          ? 'No offset changes this session'
          : commandLog.map((entry) => <span key={`${entry.at}-${entry.command}`}>{entry.at} {entry.command}</span>)}
      </div>
    </section>
  )
}

function ConfirmButton({ command, label, disabled, armed, onConfirm }) {
  const text = label || command.toUpperCase()

  return (
    <button
      type="button"
      className={`pilot-button confirm-button ${armed ? 'armed' : ''}`}
      disabled={disabled}
      onClick={() => onConfirm(command)}
    >
      <span>{text}</span>
      <small>{armed ? 'Tap again' : 'Confirm'}</small>
    </button>
  )
}

function ElectricalWarnings({ data }) {
  const batteryValues = [data.battery0, data.battery1].filter((value) => typeof value === 'number')
  const low = batteryValues.some((value) => value < 12.2)
  const diff =
    typeof data.battery0 === 'number' && typeof data.battery1 === 'number'
      ? Math.abs(data.battery0 - data.battery1)
      : null
  const highDiff = typeof diff === 'number' && diff > 0.35
  return (
    <section className={`warning-band ${low || highDiff ? 'active' : ''}`}>
      <strong>{low || highDiff ? 'Electrical warning' : 'Electrical normal'}</strong>
      <span>
        {low ? 'One battery is below 12.2 V. ' : ''}
        {highDiff
          ? 'Battery voltage difference is high.'
          : `Voltage difference ${typeof diff === 'number' ? diff.toFixed(2) : '--'} V.`}
      </span>
    </section>
  )
}

function DebugBlock({ title, lines }) {
  return (
    <article className="debug-block">
      <h3>{title}</h3>
      <pre>{lines.join('\n')}</pre>
    </article>
  )
}

function PacketCounters({ data }) {
  return (
    <article className="debug-block">
      <h3>Packet Counters</h3>
      <dl>
        <div><dt>SeaTalk raw</dt><dd>{formatValue(data.packetCounters.seatalkRaw)}</dd></div>
        <div><dt>SeaTalk decoded</dt><dd>{formatValue(data.packetCounters.seatalkDecoded)}</dd></div>
        <div><dt>N2K PGNs</dt><dd>{formatValue(data.packetCounters.n2kPgn)}</dd></div>
        <div><dt>Bus errors</dt><dd>{formatValue(data.packetCounters.errors)}</dd></div>
        <div><dt>SeaTalk age</dt><dd>{formatAge(data.seatalkLastSeenMs)}</dd></div>
        <div><dt>N2K age</dt><dd>{formatAge(data.n2kLastSeenMs)}</dd></div>
        <div><dt>Wind age</dt><dd>{formatAge(data.windLastSeenMs)}</dd></div>
      </dl>
    </article>
  )
}

function WidgetContent({ widget, data, setData, controlsEnabled, setControlsEnabled }) {
  if (widget.type === 'metric') return <MetricCard widget={widget} data={data} />
  if (widget.id === 'heroInstruments') return <HeroInstruments data={data} />
  if (widget.id === 'electricalWarnings') return <ElectricalWarnings data={data} />
  if (widget.id === 'autopilotPanel') {
    return (
      <AutopilotPanel
        data={data}
        setData={setData}
        controlsEnabled={controlsEnabled}
        setControlsEnabled={setControlsEnabled}
      />
    )
  }
  if (widget.id === 'windOffsetPanel') {
    return <WindOffsetPanel data={data} setData={setData} controlsEnabled={controlsEnabled} />
  }
  if (widget.id === 'rawSeatalk') return <DebugBlock title="Raw SeaTalk 1" lines={data.rawSeatalk} />
  if (widget.id === 'decodedSeatalk') return <DebugBlock title="Decoded SeaTalk 1" lines={data.decodedSeatalk} />
  if (widget.id === 'rawN2k') return <DebugBlock title="Raw N2K PGNs" lines={data.rawN2k} />
  if (widget.id === 'packetCounters') return <PacketCounters data={data} />
  return null
}

function LayoutSettings({ layout, setLayout, activePage }) {
  function updateActivePage(updater) {
    setLayout((current) => ({
      ...current,
      pages: current.pages.map((page) => (page.id === activePage.id ? updater(page) : page)),
    }))
  }

  function renamePage(name) {
    updateActivePage((page) => ({ ...page, name }))
  }

  function addPage() {
    const id = `page-${Date.now()}`
    const page = createPage(id, `Page ${layout.pages.length + 1}`, [])
    setLayout((current) => ({
      ...current,
      pages: [...current.pages, page],
      activePageId: id,
    }))
  }

  function deletePage() {
    if (layout.pages.length <= 1) return
    setLayout((current) => {
      const pages = current.pages.filter((page) => page.id !== activePage.id)
      return {
        pages,
        activePageId: pages[0].id,
      }
    })
  }

  function toggleWidget(widgetId, enabled) {
    updateActivePage((page) => {
      if (enabled && !page.widgets.includes(widgetId)) {
        return {
          ...page,
          widgets: [...page.widgets, widgetId],
          sizes: {
            ...page.sizes,
            [widgetId]: widgetById[widgetId]?.defaultSize || { w: 2, h: 1 },
          },
        }
      }
      if (!enabled) {
        return { ...page, widgets: page.widgets.filter((id) => id !== widgetId) }
      }
      return page
    })
  }

  function moveWidget(widgetId, direction) {
    updateActivePage((page) => {
      const index = page.widgets.indexOf(widgetId)
      const nextIndex = index + direction
      if (nextIndex < 0 || nextIndex >= page.widgets.length) return page
      const widgets = [...page.widgets]
      ;[widgets[index], widgets[nextIndex]] = [widgets[nextIndex], widgets[index]]
      return { ...page, widgets }
    })
  }

  function updateWidgetSize(widgetId, size) {
    updateActivePage((page) => ({
      ...page,
      sizes: {
        ...page.sizes,
        [widgetId]: normalizeSize(size),
      },
    }))
  }

  return (
    <section className="settings-panel" aria-label="Dashboard layout settings">
      <div className="settings-grid">
        <div className="page-editor">
          <span className="eyebrow">Pages</span>
          <label>
            Page name
            <input type="text" value={activePage.name} onChange={(event) => renamePage(event.target.value)} />
          </label>
          <div className="settings-actions">
            <button type="button" onClick={addPage}>Add Page</button>
            <button type="button" disabled={layout.pages.length <= 1} onClick={deletePage}>Delete Page</button>
            <button type="button" onClick={() => setLayout(defaultLayout)}>Reset All</button>
          </div>
        </div>
        <div>
          <span className="eyebrow">Widgets on {activePage.name}</span>
          <div className="card-config-list">
            {widgetCatalog.map((widget) => {
              const enabled = activePage.widgets.includes(widget.id)
              const index = activePage.widgets.indexOf(widget.id)
              const size = normalizeSize(activePage.sizes[widget.id] || widget.defaultSize)
              return (
                <div className="card-config-row" key={widget.id}>
                  <label>
                    <input
                      type="checkbox"
                      checked={enabled}
                      onChange={(event) => toggleWidget(widget.id, event.target.checked)}
                    />
                    {widget.label}
                  </label>
                  <div className="widget-controls">
                    <button type="button" disabled={!enabled || index === 0} onClick={() => moveWidget(widget.id, -1)}>
                      Up
                    </button>
                    <button
                      type="button"
                      disabled={!enabled || index === activePage.widgets.length - 1}
                      onClick={() => moveWidget(widget.id, 1)}
                    >
                      Down
                    </button>
                    <button
                      type="button"
                      disabled={!enabled}
                      onClick={() => updateWidgetSize(widget.id, { ...size, w: size.w === 8 ? 1 : size.w + 1 })}
                    >
                      W {formatWidth(size.w)}
                    </button>
                    <button
                      type="button"
                      disabled={!enabled}
                      onClick={() => updateWidgetSize(widget.id, { ...size, h: size.h === MAX_WIDGET_HEIGHT ? 1 : size.h + 1 })}
                    >
                      H {size.h}
                    </button>
                  </div>
                </div>
              )
            })}
          </div>
        </div>
      </div>
    </section>
  )
}

function App() {
  const [data, setData] = useMarineData()
  const [layout, setLayout] = useState(loadLayout)
  const [settingsOpen, setSettingsOpen] = useState(false)
  const [controlsEnabled, setControlsEnabled] = useState(() => {
    try {
      return Boolean(JSON.parse(localStorage.getItem(CONTROL_KEY))?.controlsEnabled)
    } catch {
      return false
    }
  })

  useEffect(() => {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(layout))
  }, [layout])

  const activePage = useMemo(() => {
    return layout.pages.find((page) => page.id === layout.activePageId) || layout.pages[0]
  }, [layout])

  const visibleWidgets = useMemo(() => {
    return activePage.widgets.map((id) => widgetById[id]).filter(Boolean)
  }, [activePage])

  function setActivePage(activePageId) {
    setLayout((current) => ({ ...current, activePageId }))
  }

  function resizeWidget(widgetId, size) {
    setLayout((current) => ({
      ...current,
      pages: current.pages.map((page) =>
        page.id === activePage.id
          ? {
              ...page,
              sizes: {
                ...page.sizes,
                [widgetId]: normalizeSize(size),
              },
            }
          : page,
      ),
    }))
  }

  return (
    <main className="app-shell">
      <header className="top-bar">
        <div>
          <span className="eyebrow">M5Stamp PLC</span>
          <h1>Marine Dashboard</h1>
        </div>
        <button type="button" className="secondary-button" onClick={() => setSettingsOpen((open) => !open)}>
          Layout
        </button>
      </header>

      <nav className="tab-bar" aria-label="Dashboard pages">
        {layout.pages.map((page) => (
          <button
            type="button"
            key={page.id}
            className={activePage.id === page.id ? 'active' : ''}
            onClick={() => setActivePage(page.id)}
          >
            {page.name}
          </button>
        ))}
      </nav>

      <StatusStrip data={data} />

      {settingsOpen && <LayoutSettings layout={layout} setLayout={setLayout} activePage={activePage} />}

      <section className="dashboard-grid" aria-label={`${activePage.name} dashboard widgets`}>
        {visibleWidgets.length === 0 && (
          <div className="empty-dashboard">
            <strong>{activePage.name} is empty</strong>
            <span>Open Layout and add widgets to this page.</span>
          </div>
        )}
        {visibleWidgets.map((widget) => (
          <DashboardTile
            key={widget.id}
            widget={widget}
            size={normalizeSize(activePage.sizes[widget.id] || widget.defaultSize)}
            onResize={resizeWidget}
          >
            <WidgetContent
              widget={widget}
              data={data}
              setData={setData}
              controlsEnabled={controlsEnabled}
              setControlsEnabled={setControlsEnabled}
            />
          </DashboardTile>
        ))}
      </section>
    </main>
  )
}

export default App
