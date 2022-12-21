var JSCOMMAPIs = {
  LOG: function(
    data //XHR REQUEST
  ) {
    console.log('%c' + data, 'background: #101; color: #00CC00');
  },
  iLOG: function(
    data //INFO
  ) {
    console.log('%c' + data, 'background: #101; color: #1CE');
  },
  rLOG: function(
    data //XHR RESPONSE
  ) {
    console.log('%c' + data, 'background: #101; color: #FF9900');
  },
  wsLOG: function(
    data //WS REQUEST
  ) {
    console.log('%c' + data, 'background: #10aded; color: #FFF');
  },
  wsrLOG: function(
    data //WS REQUEST
  ) {
    console.log('%c' + data, 'background: #feca13; color: #FFF');
  },
  mLOG: function(
    data //MESSAGE EVENT
  ) {
    console.log('%c' + data, 'background: #FFD700; color: #0000');
  },
  eLOG: function(
    data //ERROR
  ) {
    console.log('%c' + data, 'background: #fa113d; color: #FFF');
  },

  Hooks: function() {
    var c = 0; //XHR Counter
    var ws = 0; //WS Counter

    (function () {
      //XHR - https://developer.mozilla.org/en-US/docs/Web/API/XMLHttpRequest
      var proxied = window.XMLHttpRequest.prototype.open;
      window.XMLHttpRequest.prototype.open = function() {
        var cnt = ++c;
        JSCOMMAPIs.iLOG('XHR REQUEST ' + cnt);
        try {
          if(arguments) {
            var url = '';
            if(String(arguments[1]).indexOf('://') === -1) {
              url = window.location.href;
              if(arguments[1].startsWith('/')) {
                url = url.substring(0, url.lastIndexOf('/')) + arguments[1];
              } else {
                url = url.substring(0, url.lastIndexOf('/')) + '/' + arguments[1];
              }
            } else {
              url = arguments[1];
            }
            var d = arguments[0] + ' ' + url + '\nOPTIONAL ARGUMENTS: ';
            for(var i = 2; i < arguments.length; i++) {
              d += ' ' + arguments[i] + ' ';
            }
            JSCOMMAPIs.LOG(d);
          }
          //XHR RESPONSE
          this.addEventListener(
            'readystatechange',
            function() {
              if(this.readyState === 4) {
                JSCOMMAPIs.iLOG('XHR RESPONSE ' + cnt);
                JSCOMMAPIs.rLOG('URL: ' + url);
                JSCOMMAPIs.rLOG('RESPONSE HEADERS: ' + this.getAllResponseHeaders());
                JSCOMMAPIs.rLOG('RESPONSE DATA: ' + this.responseText); //ADD responseXML
              }
            },
            false
          );
        } catch(e) {
          JSCOMMAPIs.eLOG('Error: ' + e.message);
        }
        return proxied.apply(this, [].slice.call(arguments));
      };

      var proxied_head = window.XMLHttpRequest.prototype.setRequestHeader;
      window.XMLHttpRequest.prototype.setRequestHeader = function() {
        try {
          if(arguments) {
            var d = 'REQUEST HEADER: ' + arguments[0] + ': ' + arguments[1];
            JSCOMMAPIs.LOG(d);
          }
        } catch(e) {
          JSCOMMAPIs.eLOG('Error: ' + e.message);
        }

        return proxied_head.apply(this, [].slice.call(arguments));
      };

      var proxied_data = window.XMLHttpRequest.prototype.send;
      window.XMLHttpRequest.prototype.send = function() {
        try {
          if(arguments[0]) {
            var d = 'REQUEST DATA: ' + arguments[0];
            JSCOMMAPIs.LOG(d);
          }
        } catch(e) {
          JSCOMMAPIs.eLOG('Error: ' + e.message);
        }
        return proxied_data.apply(this, [].slice.call(arguments));
      };
      //WebSocket -  https://developer.mozilla.org/en-US/docs/Web/API/WebSocket
      var p_ws = window.WebSocket;
      var new_ws;
      window.WebSocket = function() {
        var wcnt = ++ws;
        try {
          JSCOMMAPIs.iLOG('WebSocket Connection ' + wcnt);
          if(arguments[1]) {
            new_ws = new p_ws(arguments[0], arguments[1]);
            JSCOMMAPIs.wsLOG('CONNECTING: ' + arguments[0] + ' PROTOCOLS: ' + arguments[1]);
          } else {
            new_ws = new p_ws(arguments[0]);
            JSCOMMAPIs.wsLOG('CONNECTING: ' + arguments[0]);
          }
          //WebSocket Send
          var w_send = new_ws.send;
          new_ws.send = function() {
            JSCOMMAPIs.wsLOG('WS ' + wcnt + ' REQUEST DATA: ' + arguments[0]);
            //WS RESPONSE
            new_ws.addEventListener(
              'message',
              function(msg) {
                JSCOMMAPIs.wsrLOG('WS' + wcnt + ' RESPONSE DATA: ' + msg.data);
              },
              false
            );

            w_send.apply(this, [].slice.call(arguments));
          };
          //WebSocket Close
          var w_close = new_ws.close;
          new_ws.close = function() {
            JSCOMMAPIs.iLOG('WebSocket ' + wcnt + ' Closed');
            w_close.apply(this, [].slice.call(arguments));
          };
        } catch(e) {
          JSCOMMAPIs.eLOG('Error: ' + e.message);
        }
        return new_ws;
      };
      //POSTMESSAGE API
      var p_msg = window.parent.postMessage;
      window.parent.postMessage = function() {
        console.log('postMessage API Called!');
        console.log('Message: ' + arguments[0]);
        return p_msg.apply(this, [].slice.call(arguments));
      };
      //http://www.javascripture.com/MessageEvent
      //Logging Message Event
      window.addEvent('message', function(event) {
        var x = event.event.data;
        if(typeof event.event.data === 'object') {
          data = JSON.stringify(x);
        } else {
          data = x;
        }
        JSCOMMAPIs.mLOG('Message Event\nFROM: ' + event.origin + '\nMESSAGE: ' + data);
      });

      //Function Ends
    })();
    //Hooks Ends
  }
};
Object.freeze(JSCOMMAPIs);
JSCOMMAPIs.Hooks();
