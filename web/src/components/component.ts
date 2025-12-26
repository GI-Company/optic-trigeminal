export abstract class Component {
  protected element: HTMLElement | null = null;

  abstract render(): string;

  abstract mount(container: HTMLElement): void;

  abstract unmount(): void;

  protected querySelector<T extends HTMLElement>(selector: string): T | null {
    return this.element?.querySelector<T>(selector) ?? null;
  }

  protected querySelectorAll<T extends HTMLElement>(selector: string): T[] {
    return this.element ? Array.from(this.element.querySelectorAll<T>(selector)) : [];
  }

  protected on(
    selector: string,
    event: string,
    handler: (e: Event) => void
  ): void {
    if (!this.element) return;
    const elements = this.element.querySelectorAll(selector);
    elements.forEach(el => {
      el.addEventListener(event, handler);
    });
  }

  protected once(
    selector: string,
    event: string,
    handler: (e: Event) => void
  ): void {
    if (!this.element) return;
    const elements = this.element.querySelectorAll(selector);
    elements.forEach(el => {
      el.addEventListener(event, handler, { once: true });
    });
  }
}
