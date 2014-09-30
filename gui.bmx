Type Gadget
	Field parent:Gadget
	Field x:Int, y:Int
	Field w:Int, h:Int, ox:Int, oy:Int, scissorParent:Byte = True
	Method HandleEvent:Gadget( event:Event ) Abstract
	
	Method HandleGadgetEvent( event:GadgetEvent )
		If parent <> Null
			parent.HandleGadgetEvent( event )
		EndIf
	EndMethod
	
	Method RenderExterior() Abstract
	Method RenderInterior() Abstract
	
	Method LocalX:Int( _x:Int )
		If parent <> Null
			_x = parent.LocalX( _x )
		EndIf
		
		Return _x - x - ox
	EndMethod
	
	Method LocalY:Int( _y:Int )
		If parent <> Null
			_y = parent.LocalY( _y )
		EndIf
		
		Return _y - y - oy
	EndMethod
EndType



Type GadgetContainer Extends Gadget
	Field children:TList = New TList
	
	Method AddChild( child:Gadget )
		children.AddLast( child )
		child.parent = Self
	EndMethod
	
	Method HandleEvent:Gadget( event:Event )
		If event.destination = Self
			Return Self	
		EndIf
		
		If event.destination <> Null
			event.x = event.destination.LocalX( event.x )
			event.y = event.destination.LocalY( event.y )
			Return event.destination.HandleEvent( event )
		Else
			Local champ:Gadget
			
			For Local child:Gadget = EachIn children.Reversed()
				If Contains( child.x, child.y, child.w, child.h, event.x, event.y )
					champ = child
					Exit
				EndIf
			Next
			
			If champ <> Null
				event.x = event.x - champ.x - champ.ox
				event.y = event.y - champ.y - champ.oy
				event.destination = champ.HandleEvent( event )
			EndIf
		EndIf
		
		Local out:Gadget = event.destination
		
		If out = Null
			out = Self
		EndIf
		
		Return out
	EndMethod
	
	Method RenderExterior()
	
	EndMethod
	
	Method RenderInterior()
		Local _ox:Float, _oy:Float
		Local ox:Float, oy:Float
		GetOrigin( ox, oy )
		ox = Int( ox ); oy = Int( oy )
		Local sx:Int, sy:Int, sw:Int, sh:Int
		GetViewport( sx, sy, sw, sh )
		PushScissor()
		
		For Local child:Gadget = EachIn children
			Local cx:Int = child.x + ox, cy:Int = child.y + oy, cw:Int = child.w, ch:Int = child.h
			Union( sx, sy, sw, sh, cx, cy, cw, ch )
			SetViewport cx, cy, cw, ch
			SetOrigin ox + child.x, oy + child.y
			child.RenderExterior()
			SetOrigin ox + child.x + child.ox, oy + child.y + child.oy
			child.RenderInterior()
		Next
		
		PopScissor()
		SetOrigin ox, oy
	EndMethod
EndType



Function MakeGadgetContainer:GadgetContainer( x:Int, y:Int, w:Int, h:Int )
	Local container:GadgetContainer = New GadgetContainer
	container.x = x; container.y = y; container.w = w; container.h = h
	Return container
EndFunction



Function Contains:Int( rx:Int, ry:Int, rw:Int, rh:Int, x:Int, y:Int )
	If ( x < rx ) Or ( x >= rx + rw ) Or ( y < ry ) Or ( y >= ry + rh )
		Return False
	EndIf
	
	Return True
EndFunction



Function Union( ax:Int, ay:Int, aw:Int, ah:Int Var, bx:Int Var, by:Int Var, bw:Int Var, bh:Int Var )
	Local x0:Int = Max( ax, bx )
	Local y0:Int = Max( ay, by )
	Local x1:Int = Min( ax + aw - 1, bx + bw - 1 )
	Local y1:Int = Min( ay + ah - 1, by + bh - 1 )
	bx = x0
	by = y0
	bw = x1 - x0 + 1
	bh = y1 - y0 + 1
EndFunction



Global _scissorEnabled:Byte = True
Global _sx:Int, _sy:Int, _sw:Int, _sh:Int
Global _scissors:TList = New TList



Function EnableScissor()
	If _scissorEnabled
		Return
	EndIf
	
	_scissorEnabled = True
	SetViewport _sx, _sy, _sw, _sh
EndFunction



Function DisableScissor()
	If Not _scissorEnabled
		Return
	EndIf
	
	_scissorEnabled = False
	GetViewport _sx, _sy, _sw, _sh
	SetViewport 0, 0, 640, 480
EndFunction



Function PushScissor()
	Local s:Scissor = New Scissor
	GetViewport( s.x, s.y, s.w, s.h )
	_scissors.AddLast(s)
EndFunction



Function PopScissor:Scissor()
	Local s:Scissor = Scissor( _scissors.RemoveLast() )
	SetViewport( s.x, s.y, s.w, s.h )
	Return s
EndFunction



Type Scissor
	Field x:Int, y:Int, w:Int, h:Int
EndType