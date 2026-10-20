#pragma once

inline constexpr char kCurtainPage[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Office Curtains</title>
  <style>
    :root{color-scheme:light;--bg:#f4f6f7;--surface:#fff;--text:#202428;--muted:#687078;--line:#d8dde1;--up:#2466b1;--stop:#b63c36;--down:#217552;--focus:#111}
    *{box-sizing:border-box}
    body{margin:0;background:var(--bg);color:var(--text);font-family:Arial,Helvetica,sans-serif}
    header{background:#202428;color:#fff;padding:20px 18px 18px}
    header div,main{width:min(720px,100%);margin:0 auto}
    h1{margin:0;font-size:24px;line-height:1.2;letter-spacing:0}
    #connection{margin-top:7px;color:#cbd2d7;font-size:13px;min-height:16px}
    main{padding:10px 14px 28px}
    .curtain{display:grid;grid-template-columns:minmax(100px,1fr) repeat(3,52px);gap:10px;align-items:center;padding:14px 4px;border-bottom:1px solid var(--line)}
    .curtain:last-child{border-bottom:0}
    .name{font-size:17px;font-weight:700;min-width:0}
    .name span{display:block;margin-top:3px;color:var(--muted);font-size:12px;font-weight:400}
    button{width:52px;height:46px;border:0;border-radius:6px;color:#fff;font-size:24px;line-height:1;cursor:pointer;touch-action:manipulation}
    button:focus-visible{outline:3px solid var(--focus);outline-offset:2px}
    button.pending{filter:brightness(1.15)}
    .up{background:var(--up)}.stop{background:var(--stop);font-size:19px}.down{background:var(--down)}
    #status{position:sticky;bottom:10px;margin:18px auto 0;padding:11px 14px;min-height:42px;border:1px solid var(--line);border-radius:6px;background:var(--surface);color:var(--muted);font-size:14px;box-shadow:0 2px 8px rgba(0,0,0,.08)}
    #status.ok{border-color:#78a995;color:#14583d}#status.error{border-color:#d79792;color:#8e2c27}
    @media(max-width:420px){header{padding-top:17px}.curtain{grid-template-columns:minmax(82px,1fr) repeat(3,46px);gap:7px}.name{font-size:16px}button{width:46px;height:44px}}
  </style>
</head>
<body>
  <header><div><h1>Office Curtains</h1><div id="connection">Controller ready</div></div></header>
  <main>
    <section id="curtains" aria-label="Curtain controls"></section>
    <div id="status" role="status" aria-live="polite">Ready</div>
  </main>
  <script>
    const root=document.getElementById('curtains');
    const statusEl=document.getElementById('status');
    const labels={up:'Raise',stop:'Stop',down:'Lower'};
    const icons={up:'\u2191',stop:'\u25a0',down:'\u2193'};
    for(let curtain=1;curtain<=5;curtain++){
      const row=document.createElement('div');row.className='curtain';
      row.innerHTML=`<div class="name">Curtain ${curtain}<span>Office channel ${curtain}</span></div>`;
      for(const command of ['up','stop','down']){
        const button=document.createElement('button');button.className=command;
        button.textContent=icons[command];button.title=labels[command];
        button.setAttribute('aria-label',`${labels[command]} curtain ${curtain}`);
        button.onclick=()=>send(button,curtain,command);row.appendChild(button);
      }
      root.appendChild(row);
    }
    async function send(button,curtain,command){
      button.classList.add('pending');
      statusEl.className='';statusEl.textContent=`Queueing ${labels[command].toLowerCase()} for curtain ${curtain}...`;
      try{
        const response=await fetch(`/api/v1/curtains/${curtain}/commands`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command,request_id:`web-${Date.now()}`})});
        const result=await response.json();
        if(!response.ok||result.status!=='queued')throw new Error(result.reason||result.status||`HTTP ${response.status}`);
        statusEl.className='ok';statusEl.textContent=`Curtain ${curtain}: ${labels[command]} queued`;
      }catch(error){statusEl.className='error';statusEl.textContent=`Command failed: ${error.message}`}
      finally{button.classList.remove('pending')}
    }
  </script>
</body>
</html>
)HTML";
