import '@testing-library/jest-dom/vitest';

// jsdom doesn't implement HTMLCanvasElement.getContext; stub it so GridCanvas
// mounts without throwing in tests.
HTMLCanvasElement.prototype.getContext = () => null;
