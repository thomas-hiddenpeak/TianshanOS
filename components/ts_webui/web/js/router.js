/**
 * TianShanOS SPA Router
 * 简单的 Hash 路由器
 */

class Router {
    constructor() {
        this.routes = {};
        this.currentPage = null;
        
        window.addEventListener('hashchange', () => this.navigate());
        window.addEventListener('load', () => this.navigate());
    }
    
    register(path, loader) {
        this.routes[path] = loader;
    }
    
    navigate(path = null) {
        if (path) {
            window.location.hash = path;
            return;
        }
        
        let hash = window.location.hash.slice(1) || '/';
        
        // 更新导航高亮
        document.querySelectorAll('.nav-link').forEach(link => {
            const href = link.getAttribute('href');
            if (href === '#' + hash || (hash === '/' && href === '#/')) {
                link.classList.add('active');
            } else {
                link.classList.remove('active');
            }
        });
        
        // 查找路由
        const loader = this.routes[hash] || this.routes['/'];
        if (loader) {
            this.currentPage = loader;
            loader();
        }
    }
}

const router = new Router();
