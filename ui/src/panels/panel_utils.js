export function getPanelBounds(element) {
    if (!element) {
        return {}
    }

    const rect = element.getBoundingClientRect()
    const screenX = window.screenX ?? window.screenLeft ?? 0
    const screenY = window.screenY ?? window.screenTop ?? 0
    const offsetY = Math.max(0, window.outerHeight - window.innerHeight)

    return {
        width: rect.width,
        height: rect.height,
        left: screenX + rect.left,
        top: screenY + offsetY + rect.top
    }
}
