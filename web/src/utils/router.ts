type RouteHandler = () => void;

export interface Route {
  path: string;
  handler: RouteHandler;
}

export class Router {
  private routes: Map<string, RouteHandler> = new Map();
  private currentRoute: string = '';

  register(path: string, handler: RouteHandler): void {
    this.routes.set(path, handler);
  }

  navigate(path: string): void {
    if (!this.routes.has(path)) {
      console.warn(`Route not found: ${path}`);
      return;
    }

    this.currentRoute = path;
    const handler = this.routes.get(path)!;
    handler();
  }

  getCurrentRoute(): string {
    return this.currentRoute;
  }

  isOn(path: string): boolean {
    return this.currentRoute === path;
  }
}

export const router = new Router();
