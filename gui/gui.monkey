Strict



Import mojo



Import containergadget
Import event
Import gadget
Import gadgetevent
Import viewgadget
Import windowgadget



Function RectangleContainsPoint:Int( rx:Int, ry:Int, rw:Int, rh:Int, x:Int, y:Int )
	If ( x < rx ) Or ( x >= rx + rw ) Or ( y < ry ) Or ( y >= ry + rh )
		Return False
	EndIf
	
	Return True
End



Function RectangleUnion:Int[]( ax:Int, ay:Int, aw:Int, ah:Int, bx:Int, by:Int, bw:Int, bh:Int )
	Local x0:Int = Max( ax, bx )
	Local y0:Int = Max( ay, by )
	Local x1:Int = Min( ax + aw - 1, bx + bw - 1 )
	Local y1:Int = Min( ay + ah - 1, by + bh - 1 )
	Return [ x0, y0, x1 - x0 + 1, y1 - y0 + 1 ]
End



Global _scissorEnabled:Bool = True
Global _scissors := New List< ScissorBox >



Function EnableScissor:Void()
	If _scissorEnabled Then Return
	_scissorEnabled = True
	PopScissor()
End



Function DisableScissor:Void()
	If Not _scissorEnabled Then Return
	PushScissor()
	SetScissor 0, 0, 640, 480
	_scissorEnabled = False
End



Function PushScissor:Void()
	If Not _scissorEnabled Then EnableScissor()
	Local s:Float[] = GetScissor()
	_scissors.AddLast( New ScissorBox( s ) )
End



Function PopScissor:Void()
	If Not _scissorEnabled Then EnableScissor()
	Local _s:ScissorBox = _scissors.RemoveLast()
	Local s:Float[] = _s.value
	SetScissor( s[0], s[1], s[2], s[3] )
End



Class ScissorBox
	Field value:Float[]
	
	Method New( value:Float[] )
		Self.value = value
	End
End



#Rem
Function PushScissor()
	Local sp:=context.matrixSp
	If sp=context.matrixStack.Length context.matrixStack=context.matrixStack.Resize( sp*2 )
	context.matrixStack[sp+0]=context.ix
	context.matrixStack[sp+1]=context.iy
	context.matrixStack[sp+2]=context.jx
	context.matrixStack[sp+3]=context.jy
	context.matrixStack[sp+4]=context.tx
	context.matrixStack[sp+5]=context.ty
	context.matrixSp=sp+6
End

Function PopMatrix()
	Local sp=context.matrixSp-6
	SetMatrix context.matrixStack[sp+0],context.matrixStack[sp+1],context.matrixStack[sp+2],context.matrixStack[sp+3],context.matrixStack[sp+4],context.matrixStack[sp+5]
	context.matrixSp=sp
End
#End