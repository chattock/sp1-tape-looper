(()=>{
 const key='sp1-color-theme',root=document.documentElement;
 let choice;try{choice=localStorage.getItem(key)}catch{}
 const query=new URLSearchParams(location.search).get('theme');if(['light','dark'].includes(query)){choice=query;try{localStorage.setItem(key,choice)}catch{}const url=new URL(location.href);url.searchParams.delete('theme');history.replaceState(history.state,'',url)}
 function apply(value){choice=value;const dark=value==='dark';root.dataset.theme=dark?'dark':'light';root.style.colorScheme=dark?'dark':'light';const button=document.getElementById('colorTheme');if(button){button.setAttribute('aria-pressed',String(dark));button.title=dark?'Switch to light mode':'Switch to dark mode';button.innerHTML=dark?'☼':'☾'}window.dispatchEvent(new CustomEvent('sp1-theme-change',{detail:{dark}}));}
 apply(choice);window.addEventListener('storage',e=>{if(e.key===key||e.key===null)apply(e.newValue)});
 document.addEventListener('DOMContentLoaded',()=>{if(window.top!==window)return;const button=document.createElement('button');button.id='colorTheme';button.type='button';button.className='color-theme';button.setAttribute('aria-label','Dark mode');button.addEventListener('click',()=>{const value=root.dataset.theme==='dark'?'light':'dark';try{localStorage.setItem(key,value)}catch{}apply(value)});const actions=document.querySelector('.header-actions');if(actions)actions.prepend(button);else{button.classList.add('standalone-theme');document.body.append(button)}apply(choice)});
})();
