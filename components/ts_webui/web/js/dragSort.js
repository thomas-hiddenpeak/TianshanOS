/**
 * 长按拖拽排序工具
 * 用于设备面板的数据组件和快捷操作区域，按住 3 秒后进入拖拽模式
 */
(function() {
    'use strict';

    const HOLD_MS = 3000;
    const MOVE_THRESHOLD = 8;
    const EXCLUDE_SELECTORS = 'button, a, .dw-log-toolbar, .quick-action-nohup-bar';

    /**
     * @param {HTMLElement} container - 容器元素
     * @param {Object} options
     * @param {string} options.itemSelector - 可拖拽子项选择器
     * @param {string} options.idAttribute - ID 属性名
     * @param {number} [options.holdMs=3000] - 长按阈值(ms)
     * @param {number} [options.moveThreshold=8] - 取消长按的移动阈值(px)
     * @param {boolean} [options.updateDOM=false] - true 时工具函数在 drop 时移动 DOM
     * @param {function(oldIndex, newIndex)} options.onReorder - 排序完成回调
     * @returns {{ destroy: function }}
     */
    window.initLongPressDragSort = function(container, options) {
        if (!container || !options || !options.itemSelector || !options.onReorder) return { destroy: function() {} };

        const itemSelector = options.itemSelector;
        const holdMs = options.holdMs ?? HOLD_MS;
        const moveThreshold = options.moveThreshold ?? MOVE_THRESHOLD;
        const updateDOM = options.updateDOM ?? false;

        let state = 'IDLE';
        let holdTimer = null;
        let dragItem = null;
        let ghost = null;
        let startX = 0, startY = 0;
        let lastOverEl = null;
        let dragJustEnded = false;

        const boundMove = onDocumentPointerMove;
        const boundUp = onDocumentPointerUp;
        const boundCancel = onDocumentPointerCancel;
        const boundKeyDown = onDocumentKeyDown;

        function startPendingDocumentListeners() {
            document.addEventListener('pointermove', onContainerPointerMove);
            document.addEventListener('pointerup', onContainerPointerUp);
            document.addEventListener('pointercancel', onContainerPointerCancel);
        }

        function stopPendingDocumentListeners() {
            document.removeEventListener('pointermove', onContainerPointerMove);
            document.removeEventListener('pointerup', onContainerPointerUp);
            document.removeEventListener('pointercancel', onContainerPointerCancel);
        }

        function clearPendingState() {
            state = 'IDLE';
            if (holdTimer) {
                clearTimeout(holdTimer);
                holdTimer = null;
            }
            if (dragItem) {
                dragItem.classList.remove('drag-pending');
                dragItem.style.userSelect = '';
                dragItem.style.webkitUserSelect = '';
                dragItem = null;
            }
            stopPendingDocumentListeners();
        }

        function cleanup() {
            state = 'IDLE';
            if (holdTimer) {
                clearTimeout(holdTimer);
                holdTimer = null;
            }
            if (ghost && ghost.parentNode) {
                ghost.parentNode.removeChild(ghost);
                ghost = null;
            }
            if (dragItem) {
                dragItem.classList.remove('drag-pending', 'drag-active-source');
                dragItem.style.userSelect = '';
                dragItem.style.webkitUserSelect = '';
                dragItem = null;
            }
            if (lastOverEl) {
                lastOverEl.classList.remove('drag-over-before');
                lastOverEl = null;
            }
            container.style.touchAction = '';
            document.removeEventListener('pointermove', boundMove);
            document.removeEventListener('pointerup', boundUp);
            document.removeEventListener('pointercancel', boundCancel);
            document.removeEventListener('keydown', boundKeyDown);
            stopPendingDocumentListeners();
        }

        function cancelDrag() {
            cleanup();
        }

        function onDocumentPointerMove(e) {
            if (!container.isConnected) {
                cleanup();
                return;
            }
            if (state !== 'DRAGGING' || !ghost) return;
            e.preventDefault();
            ghost.style.left = e.clientX - ghost.offsetWidth / 2 + 'px';
            ghost.style.top = e.clientY - ghost.offsetHeight / 2 + 'px';

            const items = [...container.querySelectorAll(itemSelector)];
            const idx = items.indexOf(dragItem);
            if (idx === -1) return;

            const cx = e.clientX, cy = e.clientY;
            let dropIdx = items.length;
            for (let i = 0; i < items.length; i++) {
                if (items[i] === dragItem) continue;
                const r = items[i].getBoundingClientRect();
                const midY = r.top + r.height / 2;
                const midX = r.left + r.width / 2;
                if (cy < midY || (Math.abs(cy - midY) < 2 && cx < midX)) {
                    dropIdx = i;
                    break;
                }
            }

            if (lastOverEl) {
                lastOverEl.classList.remove('drag-over-before');
                lastOverEl = null;
            }
            if (dropIdx < items.length && items[dropIdx] !== dragItem) {
                lastOverEl = items[dropIdx];
                lastOverEl.classList.add('drag-over-before');
            }
        }

        function onDocumentPointerUp(e) {
            if (!container.isConnected) {
                cleanup();
                return;
            }
            if (state !== 'DRAGGING' || !dragItem) return;
            e.preventDefault();

            const items = [...container.querySelectorAll(itemSelector)];
            const oldIdx = items.indexOf(dragItem);
            if (oldIdx === -1) {
                cleanup();
                return;
            }

            const cx = e.clientX, cy = e.clientY;
            let dropIdx = items.length;
            for (let i = 0; i < items.length; i++) {
                if (items[i] === dragItem) continue;
                const r = items[i].getBoundingClientRect();
                const midY = r.top + r.height / 2;
                const midX = r.left + r.width / 2;
                if (cy < midY || (Math.abs(cy - midY) < 2 && cx < midX)) {
                    dropIdx = i;
                    break;
                }
            }

            let adjustedNewIdx = dropIdx;
            if (dropIdx > oldIdx) adjustedNewIdx = dropIdx - 1;
            if (adjustedNewIdx === oldIdx) {
                cleanup();
                return;
            }

            if (updateDOM) {
                const targetItem = dropIdx < items.length ? items[dropIdx] : null;
                if (targetItem) {
                    container.insertBefore(dragItem, targetItem);
                } else {
                    container.appendChild(dragItem);
                }
            }

            dragJustEnded = true;
            cleanup();
            options.onReorder(oldIdx, adjustedNewIdx);
            setTimeout(function() { dragJustEnded = false; }, 0);
        }

        function onDocumentPointerCancel(e) {
            if (state !== 'DRAGGING') return;
            e.preventDefault();
            cancelDrag();
        }

        function onDocumentKeyDown(e) {
            if (e.key === 'Escape' && state === 'DRAGGING') {
                e.preventDefault();
                cancelDrag();
            }
        }

        function onContainerClick(e) {
            if (dragJustEnded && container.contains(e.target)) {
                e.preventDefault();
                e.stopPropagation();
                dragJustEnded = false;
            }
        }

        function onContainerPointerDown(e) {
            if (e.button !== 0) return;
            const target = e.target;
            if (target.closest(EXCLUDE_SELECTORS)) return;

            const item = target.closest(itemSelector);
            if (!item || !container.contains(item)) return;
            if (state !== 'IDLE') return;

            const items = [...container.querySelectorAll(itemSelector)];
            if (items.length <= 1) return;

            state = 'PENDING';
            dragItem = item;
            startX = e.clientX;
            startY = e.clientY;
            item.classList.add('drag-pending');
            item.style.userSelect = 'none';
            item.style.webkitUserSelect = 'none';
            startPendingDocumentListeners();

            holdTimer = setTimeout(function() {
                holdTimer = null;
                if (state !== 'PENDING' || !dragItem) return;
                if (!container.isConnected) {
                    cleanup();
                    return;
                }

                stopPendingDocumentListeners();
                state = 'DRAGGING';
                dragItem.classList.remove('drag-pending');
                dragItem.classList.add('drag-active-source');

                const rect = dragItem.getBoundingClientRect();
                ghost = dragItem.cloneNode(true);
                ghost.style.cssText = 'position:fixed;pointer-events:none;opacity:0.7;z-index:9999;' +
                    'width:' + rect.width + 'px;height:' + rect.height + 'px;' +
                    'left:' + rect.left + 'px;top:' + rect.top + 'px;transition:none;';
                ghost.classList.remove('drag-pending', 'drag-active-source', 'drag-over-before');
                document.body.appendChild(ghost);

                container.style.touchAction = 'none';
                document.addEventListener('pointermove', boundMove);
                document.addEventListener('pointerup', boundUp);
                document.addEventListener('pointercancel', boundCancel);
                document.addEventListener('keydown', boundKeyDown);

                document.addEventListener('contextmenu', function preventContext(e) {
                    e.preventDefault();
                }, { once: true });
            }, holdMs);
        }

        function onContainerPointerMove(e) {
            if (state !== 'PENDING') return;
            const dx = e.clientX - startX, dy = e.clientY - startY;
            if (Math.sqrt(dx * dx + dy * dy) > moveThreshold) {
                clearPendingState();
            }
        }

        function onContainerPointerUp(e) {
            if (state !== 'PENDING') return;
            clearPendingState();
        }

        function onContainerPointerCancel(e) {
            if (state !== 'PENDING') return;
            clearPendingState();
        }

        container.addEventListener('pointerdown', onContainerPointerDown);
        container.addEventListener('pointermove', onContainerPointerMove);
        container.addEventListener('pointerup', onContainerPointerUp);
        container.addEventListener('pointercancel', onContainerPointerCancel);
        container.addEventListener('click', onContainerClick, true);

        return {
            destroy: function() {
                cleanup();
                container.removeEventListener('pointerdown', onContainerPointerDown);
                container.removeEventListener('pointermove', onContainerPointerMove);
                container.removeEventListener('pointerup', onContainerPointerUp);
                container.removeEventListener('pointercancel', onContainerPointerCancel);
                container.removeEventListener('click', onContainerClick, true);
            }
        };
    };
})();
