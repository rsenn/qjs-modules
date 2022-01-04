export function Node(obj) {
  if(!(this instanceof Node))
    return new Node(obj);
  
  
return this; 
}

export function Document(obj) {
  if(!(this instanceof Document))
    return new Document(obj);
  

return this; 
}

Document.prototype.__proto__ = Node.prototype;
