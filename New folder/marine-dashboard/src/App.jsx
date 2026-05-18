import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import './App.css'

const STORAGE_KEY = 'm5-marine-dashboard-layout-v3'
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
  { id: 'awa', label: 'Apparent Wind Angle', type: 'metric', unit: 'deg', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'sog', label: 'SOG', type: 'metric', unit: 'kt', precision: 1, defaultSize: { w: 2, h: 1 } },
  { id: 'cog', label: 'COG', type: 'metric', unit: 'deg', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'depth', label: 'Depth', type: 'metric', unit: 'm', precision: 1, defaultSize: { w: 2, h: 1 } },
  { id: 'waterTemp', label: 'Water Temp', type: 'metric', unit: 'C', precision: 1, defaultSize: { w: 2, h: 1 } },
  { id: 'rudderAngle', label: 'Rudder Angle', type: 'metric', unit: 'deg', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'battery0', label: 'Battery 0', type: 'metric', unit: 'V', precision: 2, defaultSize: { w: 2, h: 1 } },
  { id: 'battery1', label: 'Battery 1', type: 'metric', unit: 'V', precision: 2, defaultSize: { w: 2, h: 1 } },
  { id: 'batteryDiff', label: 'Battery Diff', type: 'metric', unit: 'V', precision: 2, defaultSize: { w: 2, h: 1 } },
  { id: 'autopilotMode', label: 'Autopilot Mode', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'targetHeading', label: 'Target Heading', type: 'metric', unit: 'deg', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'seatalkStatus', label: 'SeaTalk 1', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'n2kStatus', label: 'N2K', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'wifiClients', label: 'Wi-Fi Clients', type: 'metric', unit: '', precision: 0, defaultSize: { w: 2, h: 1 } },
  { id: 'electricalWarnings', label: 'Electrical Warnings', type: 'special', defaultSize: { w: 4, h: 1 } },
  { id: 'autopilotPanel', label: 'Autopilot Controls', type: 'special', defaultSize: { w: 8, h: 4 } },
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
    'seatalkStatus',
    'autopilotPanel',
  ]),
  createPage('sailing', 'Sailing', ['awa', 'heading', 'sog', 'cog', 'depth', 'waterTemp', 'rudderAngle']),
  createPage('electrical', 'Electrical', ['battery0', 'battery1', 'batteryDiff', 'electricalWarnings', 'wifiClients']),
  createPage('debug', 'Debug', ['seatalkStatus', 'n2kStatus', 'rawSeatalk', 'decodedSeatalk', 'rawN2k', 'packetCounters']),
]

const defaultLayout = {
  pages: defaultPages,
  activePageId: 'helm',
}

const initialData = {
  heading: 184,
  cog: 181,
  sog: 5.4,
  awa: 38,
  depth: 12.4,
  waterTemp: 18.6,
  rudderAngle: -3,
  battery0: 12.72,
  battery1: 12.64,
  autopilotMode: 'AUTO',
  targetHeading: 185,
  seatalkStatus: 'ok',
  n2kStatus: 'ok',
  seatalkLastSeenMs: 150,
  n2kLastSeenMs: 120,
  wifiClients: 2,
  uptime: 123456,
  packetCounters: {
    seatalkRaw: 814,
    seatalkDecoded: 773,
    n2kPgn: 432,
    errors: 0,
  },
  rawSeatalk: ['84 20 B8 00', '89 02 7A 10', '9C 10 02 F1'],
  decodedSeatalk: ['Compass heading 184 deg', 'Rudder angle -3 deg', 'Pilot mode AUTO'],
  rawN2k: ['127250 heading', '128259 speed', '128267 depth', '130306 wind'],
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

function makeMockData(previous, started) {
  const t = (Date.now() - started) / 1000
  const heading = clampHeading(184 + Math.sin(t / 8) * 6)
  const cog = clampHeading(181 + Math.sin(t / 9) * 5)
  const sog = 5.4 + Math.sin(t / 6) * 0.4
  const awa = Math.round(38 + Math.sin(t / 4) * 9)
  const rudderAngle = Math.round(Math.sin(t / 2.8) * 8)
  const battery0 = 12.72 - t * 0.0008 + Math.sin(t / 10) * 0.02
  const battery1 = 12.64 - t * 0.0006 + Math.cos(t / 12) * 0.02

  return {
    ...previous,
    heading,
    cog,
    sog,
    awa,
    depth: 12.4 + Math.sin(t / 7) * 0.25,
    waterTemp: 18.6 + Math.sin(t / 22) * 0.18,
    rudderAngle,
    battery0,
    battery1,
    batteryDiff: battery0 - battery1,
    targetHeading: clampHeading(heading + 1),
    seatalkLastSeenMs: 90 + Math.round(Math.abs(Math.sin(t)) * 260),
    n2kLastSeenMs: 110 + Math.round(Math.abs(Math.cos(t / 1.4)) * 220),
    wifiClients: 2 + (Math.floor(t / 18) % 2),
    uptime: initialData.uptime + Math.round(t * 1000),
    packetCounters: {
      seatalkRaw: initialData.packetCounters.seatalkRaw + Math.floor(t * 7),
      seatalkDecoded: initialData.packetCounters.seatalkDecoded + Math.floor(t * 6),
      n2kPgn: initialData.packetCounters.n2kPgn + Math.floor(t * 5),
      errors: 0,
    },
    rawSeatalk: [
      `84 20 ${heading.toString(16).padStart(2, '0').toUpperCase()} 00`,
      `89 02 ${(128 + rudderAngle).toString(16).padStart(2, '0').toUpperCase()} 10`,
      previous.autopilotMode === 'STANDBY' ? '9C 00 02 F1' : '9C 10 02 F1',
    ],
    decodedSeatalk: [
      `Compass heading ${heading} deg`,
      `Rudder angle ${rudderAngle} deg`,
      `Pilot mode ${previous.autopilotMode}`,
    ],
  }
}

function useMarineData() {
  const [data, setData] = useState(initialData)
  const apiOnline = useRef(false)

  useEffect(() => {
    const started = Date.now()
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
          apiOnline.current = true
          setData((previous) => ({ ...previous, ...nextData }))
        }
      } catch {
        apiOnline.current = false
      } finally {
        window.clearTimeout(timeout)
      }
    }

    pollApi()

    const interval = window.setInterval(() => {
      pollApi()
      if (!apiOnline.current) {
        setData((previous) => makeMockData(previous, started))
      }
    }, 1000)

    return () => {
      cancelled = true
      window.clearInterval(interval)
    }
  }, [])

  return [data, setData]
}

function StatusStrip({ data }) {
  const seatalkStale = data.seatalkLastSeenMs > STALE_MS
  const n2kStale = data.n2kLastSeenMs > STALE_MS

  return (
    <section className="status-strip" aria-label="Network status">
      <div>
        <span className={seatalkStale ? 'status-dot bad' : 'status-dot'} />
        SeaTalk 1 {data.seatalkStatus} · {data.seatalkLastSeenMs} ms
      </div>
      <div>
        <span className={n2kStale ? 'status-dot bad' : 'status-dot'} />
        N2K {data.n2kStatus} · {data.n2kLastSeenMs} ms
      </div>
      <div>AP clients {data.wifiClients}</div>
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
  const value = widget.id === 'batteryDiff' ? data.battery0 - data.battery1 : data[widget.id]
  const isVoltageWarning =
    (widget.id === 'battery0' || widget.id === 'battery1') && typeof value === 'number' && value < 12.2
  const statusClass = value === 'ok' ? 'good' : value === 'warn' ? 'warn' : ''

  return (
    <article className={`metric-card ${isVoltageWarning ? 'warning' : ''}`}>
      <span className="metric-label">{widget.label}</span>
      <AutoFitText
        className={statusClass}
        value={formatValue(value, widget.precision)}
        unit={widget.unit}
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
          <AutoFitText value={formatValue(data.awa, 0)} unit="deg" min={18} max={180} />
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

function AutopilotPanel({ data, setData, controlsEnabled, setControlsEnabled }) {
  const [cooldownActive, setCooldownActive] = useState(false)
  const [armedCommand, setArmedCommand] = useState('')
  const [commandLog, setCommandLog] = useState([])
  const cooldownTimer = useRef(null)
  const confirmTimer = useRef(null)
  const disabledByStale = data.seatalkStatus !== 'ok' || data.seatalkLastSeenMs > STALE_MS
  const controlsLocked = !controlsEnabled || disabledByStale || cooldownActive

  useEffect(() => {
    localStorage.setItem(CONTROL_KEY, JSON.stringify({ controlsEnabled }))
  }, [controlsEnabled])

  useEffect(() => {
    return () => {
      if (cooldownTimer.current) window.clearTimeout(cooldownTimer.current)
      if (confirmTimer.current) window.clearTimeout(confirmTimer.current)
    }
  }, [])

  function appendLog(command) {
    setCommandLog((entries) => [{ at: new Date().toLocaleTimeString(), command }, ...entries.slice(0, 4)])
  }

  function sendCommand(command, value) {
    if (cooldownActive) return
    setCooldownActive(true)
    cooldownTimer.current = window.setTimeout(() => {
      setCooldownActive(false)
      cooldownTimer.current = null
    }, COMMAND_COOLDOWN_MS)
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
      if (command === 'heading_delta') {
        return { ...previous, targetHeading: clampHeading(previous.targetHeading + value) }
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
          <h2>{data.autopilotMode}</h2>
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
  const low = [data.battery0, data.battery1].some((value) => value < 12.2)
  const diff = Math.abs(data.battery0 - data.battery1)
  return (
    <section className={`warning-band ${low || diff > 0.35 ? 'active' : ''}`}>
      <strong>{low || diff > 0.35 ? 'Electrical warning' : 'Electrical normal'}</strong>
      <span>
        {low ? 'One battery is below 12.2 V. ' : ''}
        {diff > 0.35 ? 'Battery voltage difference is high.' : `Voltage difference ${diff.toFixed(2)} V.`}
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
        <div><dt>SeaTalk raw</dt><dd>{data.packetCounters.seatalkRaw}</dd></div>
        <div><dt>SeaTalk decoded</dt><dd>{data.packetCounters.seatalkDecoded}</dd></div>
        <div><dt>N2K PGNs</dt><dd>{data.packetCounters.n2kPgn}</dd></div>
        <div><dt>Bus errors</dt><dd>{data.packetCounters.errors}</dd></div>
        <div><dt>SeaTalk age</dt><dd>{data.seatalkLastSeenMs} ms</dd></div>
        <div><dt>N2K age</dt><dd>{data.n2kLastSeenMs} ms</dd></div>
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
