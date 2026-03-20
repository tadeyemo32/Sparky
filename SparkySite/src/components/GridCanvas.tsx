import React, { useEffect, useRef } from 'react';

const COLS = 28;
const ROWS = 18;
const GRAVITY_RADIUS = 220;
const GRAVITY_STRENGTH = 60;
const SPRING = 0.08;
const DAMPING = 0.82;

export function GridCanvas() {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    let W = 0;
    let H = 0;
    const mouse = { x: -9999, y: -9999 };
    let isDragging = false;
    type Point = { ox: number; oy: number; x: number; y: number; vx: number; vy: number };
    let points: Point[] = [];
    let animId = 0;

    function buildGrid() {
      points = [];
      const gapX = W / (COLS - 1);
      const gapY = H / (ROWS - 1);
      for (let r = 0; r < ROWS; r++) {
        for (let c = 0; c < COLS; c++) {
          const x = c * gapX;
          const y = r * gapY;
          points.push({ ox: x, oy: y, x, y, vx: 0, vy: 0 });
        }
      }
    }

    function resize() {
      canvas!.width = W = window.innerWidth;
      canvas!.height = H = window.innerHeight;
      buildGrid();
    }

    function update() {
      for (const p of points) {
        const dx = mouse.x - p.ox;
        const dy = mouse.y - p.oy;
        const dist = Math.sqrt(dx * dx + dy * dy);
        let tx = p.ox;
        let ty = p.oy;
        if (dist < GRAVITY_RADIUS && dist > 0) {
          const factor = 1 - dist / GRAVITY_RADIUS;
          const pull = factor * factor * GRAVITY_STRENGTH;
          tx = p.ox + (dx / dist) * pull;
          ty = p.oy + (dy / dist) * pull;
        }
        const fx = (tx - p.x) * SPRING;
        const fy = (ty - p.y) * SPRING;
        p.vx = (p.vx + fx) * DAMPING;
        p.vy = (p.vy + fy) * DAMPING;
        p.x += p.vx;
        p.y += p.vy;
      }
    }

    function draw() {
      ctx!.clearRect(0, 0, W, H);

      for (let r = 0; r < ROWS; r++) {
        ctx!.beginPath();
        for (let c = 0; c < COLS; c++) {
          const p = points[r * COLS + c];
          if (c === 0) ctx!.moveTo(p.x, p.y);
          else ctx!.lineTo(p.x, p.y);
        }
        ctx!.strokeStyle = 'rgba(255,255,255,0.1)';
        ctx!.lineWidth = 0.7;
        ctx!.stroke();
      }

      for (let c = 0; c < COLS; c++) {
        ctx!.beginPath();
        for (let r = 0; r < ROWS; r++) {
          const p = points[r * COLS + c];
          if (r === 0) ctx!.moveTo(p.x, p.y);
          else ctx!.lineTo(p.x, p.y);
        }
        ctx!.strokeStyle = 'rgba(255,255,255,0.07)';
        ctx!.lineWidth = 0.5;
        ctx!.stroke();
      }

      for (let r = 0; r < ROWS; r++) {
        for (let c = 0; c < COLS - 1; c++) {
          const p = points[r * COLS + c];
          const ddx = p.x - p.ox;
          const ddy = p.y - p.oy;
          const disp = Math.sqrt(ddx * ddx + ddy * ddy);
          if (disp > 1) {
            const alpha = Math.min(disp / 30, 1) * 0.25;
            ctx!.beginPath();
            ctx!.moveTo(p.x, p.y);
            ctx!.lineTo(points[r * COLS + c + 1].x, points[r * COLS + c + 1].y);
            ctx!.strokeStyle = `rgba(255,255,255,${alpha})`;
            ctx!.lineWidth = 1;
            ctx!.stroke();
          }
        }
      }
    }

    function loop() {
      update();
      draw();
      animId = requestAnimationFrame(loop);
    }

    const onResize = () => resize();
    const onMouseDown = (e: MouseEvent) => { isDragging = true; mouse.x = e.clientX; mouse.y = e.clientY; };
    const onMouseMove = (e: MouseEvent) => { if (isDragging) { mouse.x = e.clientX; mouse.y = e.clientY; } };
    const onMouseUp = () => { isDragging = false; mouse.x = -9999; mouse.y = -9999; };
    const onMouseLeave = () => { isDragging = false; mouse.x = -9999; mouse.y = -9999; };

    window.addEventListener('resize', onResize);
    window.addEventListener('mousedown', onMouseDown);
    window.addEventListener('mousemove', onMouseMove);
    window.addEventListener('mouseup', onMouseUp);
    window.addEventListener('mouseleave', onMouseLeave);

    resize();
    loop();

    return () => {
      cancelAnimationFrame(animId);
      window.removeEventListener('resize', onResize);
      window.removeEventListener('mousedown', onMouseDown);
      window.removeEventListener('mousemove', onMouseMove);
      window.removeEventListener('mouseup', onMouseUp);
      window.removeEventListener('mouseleave', onMouseLeave);
    };
  }, []);

  return (
    <canvas
      ref={canvasRef}
      style={{ position: 'fixed', inset: 0, zIndex: 0, pointerEvents: 'none' }}
    />
  );
}
