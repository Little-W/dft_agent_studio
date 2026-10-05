import QtQuick

Item {
    id: root
    property string name: "grid"
    property color color: "#56616f"
    property real stroke: Math.max(1.5, Math.min(width, height) * 0.105)
    implicitWidth: 20
    implicitHeight: 20

    Behavior on rotation {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            const w = width
            const h = height
            const p = Math.min(w, h) * 0.2
            ctx.clearRect(0, 0, w, h)
            ctx.lineWidth = root.stroke
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.strokeStyle = root.color
            ctx.fillStyle = root.color
            function line(x1, y1, x2, y2) { ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke() }
            function roundRect(x, y, rw, rh, r, filled) {
                const rr = Math.min(r, rw / 2, rh / 2)
                ctx.beginPath()
                ctx.moveTo(x + rr, y)
                ctx.lineTo(x + rw - rr, y)
                ctx.quadraticCurveTo(x + rw, y, x + rw, y + rr)
                ctx.lineTo(x + rw, y + rh - rr)
                ctx.quadraticCurveTo(x + rw, y + rh, x + rw - rr, y + rh)
                ctx.lineTo(x + rr, y + rh)
                ctx.quadraticCurveTo(x, y + rh, x, y + rh - rr)
                ctx.lineTo(x, y + rr)
                ctx.quadraticCurveTo(x, y, x + rr, y)
                filled ? ctx.fill() : ctx.stroke()
            }
            if (root.name === "grid") {
                for (let x = 0; x < 2; x++) for (let y = 0; y < 2; y++) roundRect(p + x * w * 0.42, p + y * h * 0.42, w * 0.22, h * 0.22, 2, false)
            } else if (root.name === "projects") {
                roundRect(p * 0.55, h * 0.35, w * 0.69, h * 0.49, 3, false); line(p * 0.55, h * 0.48, w * 0.95, h * 0.48); line(w * 0.23, h * 0.35, w * 0.46, h * 0.35)
            } else if (root.name === "folder") {
                roundRect(w * 0.16, h * 0.32, w * 0.68, h * 0.48, 3, false); line(w * 0.18, h * 0.43, w * 0.82, h * 0.43); line(w * 0.26, h * 0.32, w * 0.47, h * 0.32)
            } else if (root.name === "file") {
                roundRect(w * 0.25, h * 0.13, w * 0.5, h * 0.74, 3, false); line(w * 0.39, h * 0.4, w * 0.62, h * 0.4); line(w * 0.39, h * 0.57, w * 0.62, h * 0.57)
            } else if (root.name === "code") {
                line(w * 0.42, h * 0.25, w * 0.2, h * 0.5); line(w * 0.2, h * 0.5, w * 0.42, h * 0.75); line(w * 0.58, h * 0.25, w * 0.8, h * 0.5); line(w * 0.8, h * 0.5, w * 0.58, h * 0.75)
            } else if (root.name === "flow") {
                line(p, h * 0.72, w * 0.4, h * 0.48); line(w * 0.4, h * 0.48, w * 0.62, h * 0.61); line(w * 0.62, h * 0.61, w - p, h * 0.27); roundRect(p * 0.55, h * 0.62, w * 0.18, h * 0.18, 2, true); roundRect(w * 0.5, h * 0.5, w * 0.18, h * 0.18, 2, true); roundRect(w * 0.75, h * 0.16, w * 0.18, h * 0.18, 2, true)
            } else if (root.name === "synthesis") {
                roundRect(w * 0.26, h * 0.24, w * 0.48, h * 0.52, 3, false); for (let i = 0; i < 3; i++) { const y = h * (0.34 + i * 0.16); line(w * 0.12, y, w * 0.26, y); line(w * 0.74, y, w * 0.88, y) }; line(w * 0.39, h * 0.43, w * 0.49, h * 0.53); line(w * 0.49, h * 0.53, w * 0.63, h * 0.37); line(w * 0.49, h * 0.53, w * 0.62, h * 0.65)
            } else if (root.name === "scan") {
                for (let i = 0; i < 3; i++) roundRect(w * (0.12 + i * 0.31), h * 0.36, w * 0.2, h * 0.28, 2, false); line(w * 0.32, h * 0.5, w * 0.43, h * 0.5); line(w * 0.63, h * 0.5, w * 0.74, h * 0.5); line(w * 0.84, h * 0.36, w * 0.84, h * 0.22); line(w * 0.84, h * 0.22, w * 0.16, h * 0.22); line(w * 0.16, h * 0.22, w * 0.16, h * 0.36)
            } else if (root.name === "mbist") {
                roundRect(w * 0.2, h * 0.16, w * 0.6, h * 0.68, 3, false); line(w * 0.2, h * 0.37, w * 0.8, h * 0.37); line(w * 0.2, h * 0.58, w * 0.8, h * 0.58); line(w * 0.42, h * 0.17, w * 0.42, h * 0.83); line(w * 0.62, h * 0.17, w * 0.62, h * 0.83); line(w * 0.49, h * 0.7, w * 0.56, h * 0.77); line(w * 0.56, h * 0.77, w * 0.7, h * 0.64)
            } else if (root.name === "atpg") {
                ctx.beginPath(); ctx.arc(w * 0.5, h * 0.5, w * 0.34, 0, Math.PI * 2); ctx.stroke(); ctx.beginPath(); ctx.arc(w * 0.5, h * 0.5, w * 0.19, 0, Math.PI * 2); ctx.stroke(); ctx.beginPath(); ctx.arc(w * 0.5, h * 0.5, w * 0.055, 0, Math.PI * 2); ctx.fill(); line(w * 0.5, h * 0.08, w * 0.5, h * 0.24); line(w * 0.76, h * 0.5, w * 0.92, h * 0.5)
            } else if (root.name === "lbist") {
                roundRect(w * 0.13, h * 0.34, w * 0.22, h * 0.3, 2, false); roundRect(w * 0.4, h * 0.34, w * 0.22, h * 0.3, 2, false); roundRect(w * 0.67, h * 0.34, w * 0.22, h * 0.3, 2, false); line(w * 0.35, h * 0.49, w * 0.4, h * 0.49); line(w * 0.62, h * 0.49, w * 0.67, h * 0.49); line(w * 0.78, h * 0.34, w * 0.78, h * 0.18); line(w * 0.78, h * 0.18, w * 0.24, h * 0.18); line(w * 0.24, h * 0.18, w * 0.24, h * 0.34)
            } else if (root.name === "agent") {
                roundRect(w * 0.22, h * 0.28, w * 0.56, h * 0.52, 5, false); ctx.beginPath(); ctx.arc(w * 0.4, h * 0.53, w * 0.06, 0, Math.PI * 2); ctx.fill(); ctx.beginPath(); ctx.arc(w * 0.6, h * 0.53, w * 0.06, 0, Math.PI * 2); ctx.fill(); line(w * 0.5, h * 0.16, w * 0.5, h * 0.28); ctx.beginPath(); ctx.arc(w * 0.5, h * 0.13, w * 0.05, 0, Math.PI * 2); ctx.fill()
            } else if (root.name === "info") {
                ctx.beginPath(); ctx.arc(w * 0.5, h * 0.5, Math.min(w, h) * 0.37, 0, Math.PI * 2); ctx.stroke()
                ctx.beginPath(); ctx.arc(w * 0.5, h * 0.32, root.stroke * 0.7, 0, Math.PI * 2); ctx.fill()
                line(w * 0.5, h * 0.46, w * 0.5, h * 0.7)
            } else if (root.name === "sun") {
                ctx.beginPath(); ctx.arc(w * 0.5, h * 0.5, w * 0.22, 0, Math.PI * 2); ctx.stroke()
                for (let i = 0; i < 8; i++) {
                    const angle = i * Math.PI / 4
                    line(w * (0.5 + Math.cos(angle) * 0.34), h * (0.5 + Math.sin(angle) * 0.34),
                         w * (0.5 + Math.cos(angle) * 0.46), h * (0.5 + Math.sin(angle) * 0.46))
                }
            } else if (root.name === "moon") {
                ctx.beginPath(); ctx.arc(w * 0.48, h * 0.5, w * 0.34, -Math.PI * 0.42, Math.PI * 0.42); ctx.stroke()
                ctx.beginPath(); ctx.arc(w * 0.66, h * 0.37, w * 0.27, Math.PI * 0.58, Math.PI * 1.42); ctx.stroke()
            } else if (root.name === "terminal") {
                line(w * 0.25, h * 0.3, w * 0.44, h * 0.5); line(w * 0.44, h * 0.5, w * 0.25, h * 0.7); line(w * 0.56, h * 0.7, w * 0.79, h * 0.7)
            } else if (root.name === "bolt") {
                line(w * 0.58, h * 0.1, w * 0.28, h * 0.52); line(w * 0.28, h * 0.52, w * 0.52, h * 0.52); line(w * 0.52, h * 0.52, w * 0.42, h * 0.9); line(w * 0.42, h * 0.9, w * 0.78, h * 0.38); line(w * 0.78, h * 0.38, w * 0.52, h * 0.38); line(w * 0.52, h * 0.38, w * 0.58, h * 0.1)
            } else if (root.name === "skills") {
                ctx.beginPath(); ctx.arc(w * 0.43, h * 0.45, w * 0.25, -0.65, 1.2); ctx.stroke(); line(w * 0.57, h * 0.62, w * 0.82, h * 0.87); ctx.beginPath(); ctx.arc(w * 0.43, h * 0.45, w * 0.08, 0, Math.PI * 2); ctx.stroke()
            } else if (root.name === "editor") {
                roundRect(w * 0.12, h * 0.18, w * 0.76, h * 0.64, 3, false); line(w * 0.13, h * 0.34, w * 0.87, h * 0.34); ctx.beginPath(); ctx.arc(w * 0.22, h * 0.26, w * 0.025, 0, Math.PI * 2); ctx.fill(); ctx.beginPath(); ctx.arc(w * 0.31, h * 0.26, w * 0.025, 0, Math.PI * 2); ctx.fill(); line(w * 0.36, h * 0.47, w * 0.25, h * 0.58); line(w * 0.25, h * 0.58, w * 0.36, h * 0.69); line(w * 0.64, h * 0.47, w * 0.75, h * 0.58); line(w * 0.75, h * 0.58, w * 0.64, h * 0.69); line(w * 0.47, h * 0.7, w * 0.55, h * 0.46)
            } else if (root.name === "report") {
                roundRect(w * 0.22, h * 0.11, w * 0.56, h * 0.78, 3, false); line(w * 0.34, h * 0.31, w * 0.66, h * 0.31); line(w * 0.34, h * 0.44, w * 0.58, h * 0.44); line(w * 0.35, h * 0.72, w * 0.35, h * 0.61); line(w * 0.5, h * 0.72, w * 0.5, h * 0.54); line(w * 0.65, h * 0.72, w * 0.65, h * 0.48); line(w * 0.32, h * 0.72, w * 0.69, h * 0.72)
            } else if (root.name === "evidence") {
                roundRect(w * 0.24, h * 0.14, w * 0.52, h * 0.72, 3, false); line(w * 0.36, h * 0.42, w * 0.47, h * 0.55); line(w * 0.47, h * 0.55, w * 0.68, h * 0.32); line(w * 0.36, h * 0.7, w * 0.66, h * 0.7)
            } else if (root.name === "trash") {
                roundRect(w * 0.28, h * 0.31, w * 0.44, h * 0.53, 2, false); line(w * 0.22, h * 0.25, w * 0.78, h * 0.25); line(w * 0.4, h * 0.16, w * 0.6, h * 0.16); line(w * 0.42, h * 0.42, w * 0.42, h * 0.72); line(w * 0.58, h * 0.42, w * 0.58, h * 0.72)
            } else if (root.name === "save") {
                line(w * 0.5, h * 0.14, w * 0.5, h * 0.61); line(w * 0.32, h * 0.44, w * 0.5, h * 0.62); line(w * 0.5, h * 0.62, w * 0.68, h * 0.44); line(w * 0.22, h * 0.6, w * 0.22, h * 0.82); line(w * 0.22, h * 0.82, w * 0.78, h * 0.82); line(w * 0.78, h * 0.82, w * 0.78, h * 0.6)
            } else if (root.name === "arrow-up") {
                line(w * 0.5, h * 0.78, w * 0.5, h * 0.22); line(w * 0.27, h * 0.45, w * 0.5, h * 0.22); line(w * 0.5, h * 0.22, w * 0.73, h * 0.45)
            } else if (root.name === "arrow-left") {
                line(w * 0.78, h * 0.5, w * 0.22, h * 0.5); line(w * 0.45, h * 0.27, w * 0.22, h * 0.5); line(w * 0.22, h * 0.5, w * 0.45, h * 0.73)
            } else if (root.name === "settings") {
                ctx.beginPath(); ctx.arc(w * 0.5, h * 0.5, w * 0.16, 0, Math.PI * 2); ctx.stroke(); for (let i = 0; i < 8; i++) { const a = i * Math.PI / 4; line(w * 0.5 + Math.cos(a) * w * 0.25, h * 0.5 + Math.sin(a) * h * 0.25, w * 0.5 + Math.cos(a) * w * 0.36, h * 0.5 + Math.sin(a) * h * 0.36) }
            } else if (root.name === "play") {
                ctx.beginPath(); ctx.moveTo(w * 0.36, h * 0.22); ctx.lineTo(w * 0.78, h * 0.5); ctx.lineTo(w * 0.36, h * 0.78); ctx.closePath(); ctx.fill()
            } else if (root.name === "stop") {
                roundRect(w * 0.28, h * 0.28, w * 0.44, h * 0.44, 2, true)
            } else if (root.name === "loader") {
                ctx.beginPath(); ctx.arc(w * 0.5, h * 0.5, w * 0.34, -Math.PI * 0.78, Math.PI * 0.72); ctx.stroke()
                line(w * 0.72, h * 0.2, w * 0.86, h * 0.24)
            } else if (root.name === "plus") {
                line(w * 0.5, h * 0.22, w * 0.5, h * 0.78); line(w * 0.22, h * 0.5, w * 0.78, h * 0.5)
            } else if (root.name === "expand") {
                line(w * 0.45, h * 0.2, w * 0.22, h * 0.2); line(w * 0.22, h * 0.2, w * 0.22, h * 0.43); line(w * 0.55, h * 0.2, w * 0.78, h * 0.2); line(w * 0.78, h * 0.2, w * 0.78, h * 0.43); line(w * 0.22, h * 0.57, w * 0.22, h * 0.8); line(w * 0.22, h * 0.8, w * 0.45, h * 0.8); line(w * 0.78, h * 0.57, w * 0.78, h * 0.8); line(w * 0.55, h * 0.8, w * 0.78, h * 0.8)
            } else if (root.name === "search") {
                ctx.beginPath(); ctx.arc(w * 0.44, h * 0.44, w * 0.22, 0, Math.PI * 2); ctx.stroke(); line(w * 0.61, h * 0.61, w * 0.82, h * 0.82)
            } else if (root.name === "refresh") {
                ctx.beginPath(); ctx.arc(w * 0.5, h * 0.5, w * 0.29, -Math.PI * 0.25, Math.PI * 1.18); ctx.stroke(); line(w * 0.22, h * 0.25, w * 0.22, h * 0.44); line(w * 0.22, h * 0.25, w * 0.41, h * 0.25); ctx.beginPath(); ctx.arc(w * 0.5, h * 0.5, w * 0.29, Math.PI * 0.75, Math.PI * 2.18); ctx.stroke(); line(w * 0.78, h * 0.75, w * 0.78, h * 0.56); line(w * 0.78, h * 0.75, w * 0.59, h * 0.75)
            } else if (root.name === "chevron") {
                line(w * 0.38, h * 0.25, w * 0.62, h * 0.5); line(w * 0.62, h * 0.5, w * 0.38, h * 0.75)
            } else if (root.name === "close") {
                line(w * 0.28, h * 0.28, w * 0.72, h * 0.72); line(w * 0.72, h * 0.28, w * 0.28, h * 0.72)
            } else if (root.name === "check") {
                line(w * 0.2, h * 0.52, w * 0.42, h * 0.74); line(w * 0.42, h * 0.74, w * 0.8, h * 0.28)
            } else if (root.name === "more") {
                for (let i = 0; i < 3; i++) { ctx.beginPath(); ctx.arc(w * (0.28 + i * 0.22), h * 0.5, w * 0.055, 0, Math.PI * 2); ctx.fill() }
            } else if (root.name === "history") {
                ctx.beginPath(); ctx.arc(w * 0.53, h * 0.52, w * 0.28, -Math.PI * 0.4, Math.PI * 1.55); ctx.stroke(); line(w * 0.23, h * 0.35, w * 0.23, h * 0.18); line(w * 0.23, h * 0.18, w * 0.4, h * 0.18); line(w * 0.53, h * 0.34, w * 0.53, h * 0.54); line(w * 0.53, h * 0.54, w * 0.67, h * 0.62)
            } else if (root.name === "archive") {
                roundRect(w * 0.16, h * 0.28, w * 0.68, h * 0.54, 2, false); line(w * 0.12, h * 0.29, w * 0.88, h * 0.29); line(w * 0.4, h * 0.51, w * 0.6, h * 0.51)
            } else if (root.name === "undo") {
                ctx.beginPath(); ctx.arc(w * 0.56, h * 0.52, w * 0.27, -Math.PI * 0.2, Math.PI * 1.2); ctx.stroke(); line(w * 0.13, h * 0.38, w * 0.36, h * 0.38); line(w * 0.13, h * 0.38, w * 0.26, h * 0.25)
            } else if (root.name === "compress") {
                line(w * 0.18, h * 0.3, w * 0.42, h * 0.3); line(w * 0.18, h * 0.3, w * 0.18, h * 0.5); line(w * 0.82, h * 0.3, w * 0.58, h * 0.3); line(w * 0.82, h * 0.3, w * 0.82, h * 0.5); line(w * 0.18, h * 0.7, w * 0.42, h * 0.7); line(w * 0.18, h * 0.7, w * 0.18, h * 0.5); line(w * 0.82, h * 0.7, w * 0.58, h * 0.7); line(w * 0.82, h * 0.7, w * 0.82, h * 0.5)
            } else if (root.name === "copy") {
                roundRect(w * 0.33, h * 0.13, w * 0.48, h * 0.58, 3, false); roundRect(w * 0.19, h * 0.29, w * 0.48, h * 0.58, 3, false)
            } else if (root.name === "warning") {
                ctx.beginPath(); ctx.moveTo(w * 0.5, h * 0.12); ctx.lineTo(w * 0.9, h * 0.82); ctx.lineTo(w * 0.1, h * 0.82); ctx.closePath(); ctx.stroke()
                line(w * 0.5, h * 0.34, w * 0.5, h * 0.58); ctx.beginPath(); ctx.arc(w * 0.5, h * 0.7, w * 0.035, 0, Math.PI * 2); ctx.fill()
            } else if (root.name === "shield") {
                // Outline shield with a centered exclamation mark, matching
                // the compact approval affordance used in Copilot-like menus.
                ctx.beginPath(); ctx.moveTo(w * 0.5, h * 0.1); ctx.lineTo(w * 0.84, h * 0.23); ctx.lineTo(w * 0.8, h * 0.58); ctx.quadraticCurveTo(w * 0.74, h * 0.78, w * 0.5, h * 0.9); ctx.quadraticCurveTo(w * 0.26, h * 0.78, w * 0.2, h * 0.58); ctx.lineTo(w * 0.16, h * 0.23); ctx.closePath(); ctx.stroke()
                line(w * 0.5, h * 0.31, w * 0.5, h * 0.58); ctx.beginPath(); ctx.arc(w * 0.5, h * 0.72, w * 0.035, 0, Math.PI * 2); ctx.fill()
            } else if (root.name === "hand") {
                // Codex-style approval affordance: an upright open hand with
                // separated fingers and a compact wrist/palm silhouette.
                ctx.beginPath(); ctx.moveTo(w * 0.36, h * 0.53); ctx.lineTo(w * 0.36, h * 0.25); ctx.quadraticCurveTo(w * 0.36, h * 0.16, w * 0.42, h * 0.16); ctx.quadraticCurveTo(w * 0.48, h * 0.16, w * 0.48, h * 0.25); ctx.lineTo(w * 0.48, h * 0.12); ctx.quadraticCurveTo(w * 0.48, h * 0.04, w * 0.54, h * 0.04); ctx.quadraticCurveTo(w * 0.6, h * 0.04, w * 0.6, h * 0.12); ctx.lineTo(w * 0.6, h * 0.25); ctx.lineTo(w * 0.6, h * 0.16); ctx.quadraticCurveTo(w * 0.6, h * 0.08, w * 0.66, h * 0.08); ctx.quadraticCurveTo(w * 0.72, h * 0.08, w * 0.72, h * 0.16); ctx.lineTo(w * 0.72, h * 0.3); ctx.lineTo(w * 0.72, h * 0.24); ctx.quadraticCurveTo(w * 0.72, h * 0.16, w * 0.78, h * 0.16); ctx.quadraticCurveTo(w * 0.84, h * 0.16, w * 0.84, h * 0.24); ctx.lineTo(w * 0.84, h * 0.56); ctx.quadraticCurveTo(w * 0.84, h * 0.82, w * 0.58, h * 0.9); ctx.quadraticCurveTo(w * 0.36, h * 0.96, w * 0.24, h * 0.76); ctx.lineTo(w * 0.12, h * 0.58); ctx.quadraticCurveTo(w * 0.08, h * 0.51, w * 0.14, h * 0.47); ctx.quadraticCurveTo(w * 0.2, h * 0.43, w * 0.26, h * 0.5); ctx.lineTo(w * 0.36, h * 0.6); ctx.closePath(); ctx.stroke()
                line(w * 0.36, h * 0.53, w * 0.36, h * 0.67)
            } else if (root.name === "rocket") {
                // Compact Copilot-style rocket silhouette, kept upright in the
                // same 18 px box as the permission-menu icons.
                ctx.beginPath(); ctx.moveTo(w * 0.52, h * 0.12); ctx.quadraticCurveTo(w * 0.78, h * 0.25, w * 0.75, h * 0.58); ctx.lineTo(w * 0.53, h * 0.82); ctx.lineTo(w * 0.31, h * 0.58); ctx.quadraticCurveTo(w * 0.28, h * 0.25, w * 0.52, h * 0.12); ctx.closePath(); ctx.stroke()
                ctx.beginPath(); ctx.arc(w * 0.52, h * 0.37, w * 0.075, 0, Math.PI * 2); ctx.stroke()
                line(w * 0.31, h * 0.58, w * 0.14, h * 0.66); line(w * 0.14, h * 0.66, w * 0.24, h * 0.48)
                line(w * 0.53, h * 0.82, w * 0.53, h * 0.95); line(w * 0.53, h * 0.95, w * 0.68, h * 0.78)
                line(w * 0.43, h * 0.82, w * 0.32, h * 0.95); line(w * 0.32, h * 0.95, w * 0.32, h * 0.78)
            } else {
                roundRect(p, p, w - p * 2, h - p * 2, 4, false)
            }
        }
        Connections {
            target: root

            function onNameChanged() {
                canvas.requestPaint()
            }

            function onColorChanged() {
                canvas.requestPaint()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
}
